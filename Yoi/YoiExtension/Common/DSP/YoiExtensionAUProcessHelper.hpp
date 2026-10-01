//
//  YoiExtensionAUProcessHelper.hpp
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

#pragma once

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <span>
#include <vector>
#include "YoiExtensionDSPKernel.hpp"

//MARK:- AUProcessHelper Utility Class
class AUProcessHelper
{
public:
    AUProcessHelper(YoiExtensionDSPKernel& kernel)
    : mKernel{kernel}
    {
    }
    
    /// Call from allocateRenderResources, with the output bus's channel count and the most frames
    /// the host may ask for. This is where the memory for hosts that pass no buffers is made.
    void setChannelCount(UInt32 inputChannelCount, UInt32 outputChannelCount, AUAudioFrameCount maximumFrames)
    {
        (void)inputChannelCount;
        mOutputBuffers.resize(outputChannelCount);
        mScratchBuffers.assign(outputChannelCount, std::vector<float>(maximumFrames, 0.0f));
    }

    /**
     This function handles the event list processing and rendering loop for you.
     Call it inside your internalRenderBlock.
     */
    void processWithEvents(AudioBufferList* outBufferList, AudioTimeStamp const *timestamp, AUAudioFrameCount frameCount, AURenderEvent const *events) {

        AUEventSampleTime now = AUEventSampleTime(timestamp->mSampleTime);
        AUAudioFrameCount framesRemaining = frameCount;

        // Tempo, song position and transport, once per cycle, before any of its events or audio.
        mKernel.beginRenderCycle(now);
        AURenderEvent const *nextEvent = events; // events is a linked list, at the beginning, the nextEvent is the first event

        // Render into as many channels as both the bus format and the host's buffer list have.
        // A buffer the host left without memory (allowed: it then expects the audio unit's own)
        // gets the scratch made in setChannelCount.
        const size_t channelCount = std::min<size_t>(outBufferList->mNumberBuffers, mOutputBuffers.size());
        for (size_t channel = 0; channel < channelCount; ++channel) {
            AudioBuffer& buffer = outBufferList->mBuffers[channel];
            if (buffer.mData == nullptr) {
                buffer.mData = mScratchBuffers[channel].data();
                buffer.mDataByteSize = UInt32(frameCount * sizeof(float));
            }
        }
        const std::span<float*> outputs(mOutputBuffers.data(), channelCount);

        auto callProcess = [this, outputs] (AudioBufferList* outBufferListPtr, AUEventSampleTime now, AUAudioFrameCount frameCount, AUAudioFrameCount const frameOffset) {
            for (size_t channel = 0; channel < outputs.size(); ++channel) {
                outputs[channel] = (float*)outBufferListPtr->mBuffers[channel].mData + frameOffset;
            }

            mKernel.process(outputs, now, frameCount);
        };
        
        while (framesRemaining > 0) {
            // If there are no more events, we can process the entire remaining segment and exit.
            if (nextEvent == nullptr) {
                AUAudioFrameCount const frameOffset = frameCount - framesRemaining;
                callProcess(outBufferList, now, framesRemaining, frameOffset);
                return;
            }
            
            // **** start late events late.
            auto timeZero = AUEventSampleTime(0);
            auto headEventTime = nextEvent->head.eventSampleTime;
            AUAudioFrameCount framesThisSegment = AUAudioFrameCount(std::max(timeZero, headEventTime - now));
            
            // Compute everything before the next event.
            if (framesThisSegment > 0) {
                AUAudioFrameCount const frameOffset = frameCount - framesRemaining;
                callProcess(outBufferList, now, framesThisSegment, frameOffset);
                
                // Advance frames.
                framesRemaining -= framesThisSegment;
                
                // Advance time.
                now += AUEventSampleTime(framesThisSegment);
            }
            
            nextEvent = performAllSimultaneousEvents(now, nextEvent);
        }
    }
    
    AURenderEvent const * performAllSimultaneousEvents(AUEventSampleTime now, AURenderEvent const *event) {
        do {
            mKernel.handleOneEvent(now, event);
            
            // Go to next event.
            event = event->head.next;
            
            // While event is not null and is simultaneous (or late).
        } while (event && event->head.eventSampleTime <= now);
        return event;
    }
	
	// Block which subclassers must provide to implement rendering.
	AUInternalRenderBlock internalRenderBlock() {
		/*
		 Capture in locals to avoid ObjC member lookups. If "self" is captured in
		 render, we're doing it wrong.
		 */
		return ^AUAudioUnitStatus(AudioUnitRenderActionFlags 				*actionFlags,
								  const AudioTimeStamp       				*timestamp,
								  AUAudioFrameCount           				frameCount,
								  NSInteger                   				outputBusNumber,
								  AudioBufferList            				*outputData,
								  const AURenderEvent        				*realtimeEventListHead,
								  AURenderPullInputBlock __unsafe_unretained pullInputBlock) {
			
			if (frameCount > mKernel.maximumFramesToRender()) {
				return kAudioUnitErr_TooManyFramesToProcess;
			}
			
			/*
			 Important:
			 If the caller passed non-null output pointers (outputData->mBuffers[x].mData), use those.
			 
			 If the caller passed null output buffer pointers, process in memory owned by the Audio Unit
			 and modify the (outputData->mBuffers[x].mData) pointers to point to this owned memory.
			 The Audio Unit is responsible for preserving the validity of this memory until the next call to render,
			 or deallocateRenderResources is called.
			 
			 If your algorithm cannot process in-place, you will need to preallocate an output buffer
			 and use it here.
			 
			 See the description of the canProcessInPlace property.
			 */
			processWithEvents(outputData, timestamp, frameCount, realtimeEventListHead);
			
			return noErr;
		};
		
	}
private:
    YoiExtensionDSPKernel& mKernel;
    std::vector<float*> mOutputBuffers;
    std::vector<std::vector<float>> mScratchBuffers;
};
