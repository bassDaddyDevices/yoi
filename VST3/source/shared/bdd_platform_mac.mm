//
//  bdd_platform_mac.mm
//  Bass Daddy Devices VST3 glue
//
//  macOS side of bdd_platform.h. Built with ARC.
//

#include "bdd_platform.h"

#import <Cocoa/Cocoa.h>

namespace bdd::vst3::platform {

void attachView(void* parent, void* child, int width, int height) {
    NSView* parentView = (__bridge NSView*)parent;
    NSView* childView = (__bridge NSView*)child;
    if (parentView == nil || childView == nil) {
        return;
    }
    childView.frame = NSMakeRect(0, 0, width, height);
    childView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [parentView addSubview:childView];
}

void resizeView(void* child, int width, int height) {
    NSView* childView = (__bridge NSView*)child;
    [childView setFrame:NSMakeRect(0, 0, width, height)];
}

void detachView(void* child) {
    NSView* childView = (__bridge NSView*)child;
    [childView removeFromSuperview];
}

std::filesystem::path productDirectory(const std::string& product) {
    NSArray<NSString*>* folders =
        NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString* base = folders.firstObject ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support"];
    NSString* name = [NSString stringWithUTF8String:product.c_str()] ?: @"Unknown";
    NSString* folder = [[base stringByAppendingPathComponent:@"Bass Daddy Devices"] stringByAppendingPathComponent:name];
    return std::filesystem::path(folder.fileSystemRepresentation);
}

void revealInFileBrowser(const std::filesystem::path& folder) {
    NSString* path = [NSString stringWithUTF8String:folder.c_str()];
    if (path != nil) {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path isDirectory:YES]];
    }
}

} // namespace bdd::vst3::platform
