# AGENTS

<skills_system priority="1">

## Available Skills

<!-- SKILLS_TABLE_START -->
<usage>
When users ask you to perform tasks, check if any of the available skills below can help complete the task more effectively. Skills provide specialized capabilities and domain knowledge.

How to use skills:
- Invoke: `npx openskills read <skill-name>` (run in your shell)
  - For multiple: `npx openskills read skill-one,skill-two`
- The skill content will load with detailed instructions on how to complete the task
- Base directory provided in output for resolving bundled resources (references/, scripts/, assets/)

Usage notes:
- Only use skills listed in <available_skills> below
- Do not invoke a skill that is already loaded in your context
- Each skill invocation is stateless
</usage>

<available_skills>

<skill>
<name>ableton-extensions</name>
<description>></description>
<location>global</location>
</skill>

<skill>
<name>app-intents</name>
<description>Writes and reviews Swift App Intents code that exposes app actions and data to Siri, Shortcuts, Spotlight, widgets, Control Center, and Apple Intelligence. Use when adding AppIntent, AppEntity, OpenIntent, AppShortcutsProvider, EntityQuery, Focus Filters, AssistantEntity/AssistantIntent schemas, on-screen awareness, LongRunningIntent/CancellableIntent, EntityCollection, SyncableEntity, AppIntentsTesting, or when wiring SwiftData/networked data into intents.</description>
<location>global</location>
</skill>

<skill>
<name>frontend-design</name>
<description>Guidance for distinctive, intentional visual design when building new UI or reshaping an existing one. Helps with aesthetic direction, typography, and making choices that don't read as templated defaults.</description>
<location>global</location>
</skill>

<skill>
<name>migrate-to-notte</name>
<description>></description>
<location>global</location>
</skill>

<skill>
<name>mindrally-python-testing</name>
<description>"Expert in Python testing with pytest and test-driven development"</description>
<location>global</location>
</skill>

<skill>
<name>notte-browser</name>
<description>></description>
<location>global</location>
</skill>

<skill>
<name>notte-functions-build</name>
<description>></description>
<location>global</location>
</skill>

<skill>
<name>notte-functions-doctor</name>
<description>></description>
<location>global</location>
</skill>

<skill>
<name>open-knowledge-discovery</name>
<description>"Read when the user asks what OpenKnowledge is, wants to install it on a repository, wants to open or preview a single markdown file that is not part of an OpenKnowledge project, wants to share an OpenKnowledge project with collaborators, asks whether OpenKnowledge supports a particular capability, or asks how `ok init` / `ok cowork` / OK Desktop set up a project. Do NOT load to perform OpenKnowledge reads/writes — the runtime guidance for editing markdown inside an initialized OK project ships as a separate project-local skill installed into each detected agent's skills dir (for example `.claude/skills/open-knowledge/`) whenever `ok init` runs."</description>
<location>global</location>
</skill>

<skill>
<name>open-knowledge-write-skill</name>
<description>"Use when the user wants to create, author, write, or design a new Agent Skill (a SKILL.md) — for OpenKnowledge or for their editors — including requests like 'help me write a skill', 'make a skill that…', 'turn this workflow into a skill', or improving an existing skill's triggering and discipline. Also use when capturing reusable agent guidance that should live as an installable skill rather than a one-off prompt. Covers choosing scope (project vs global), the SKILL.md frontmatter contract, progressive-disclosure structure, evaluating the skill, and installing it into the user's editors."</description>
<location>global</location>
</skill>

<skill>
<name>swift-architecture-skill</name>
<description>Swift iOS architecture guidance and playbooks for MVVM, MVI, TCA, Clean Architecture, VIPER, MVP, Coordinator, and Reactive patterns. Use when designing, implementing, refactoring, or reviewing the architecture of a SwiftUI or UIKit feature, module, or codebase.</description>
<location>global</location>
</skill>

<skill>
<name>swift-code-reviewer</name>
<description>"Perform thorough code reviews for Swift/SwiftUI code, including spec adherence (PR description + linked issues), code quality, architecture, performance, security, Swift 6+ best practices, project standards from .claude/CLAUDE.md, and meta-feedback on recurring patterns that suggest gaps in the agent's instructions. Use when reviewing PRs/MRs (especially AI-generated ones), performing quality audits, validating against original spec, or providing structured feedback with severity levels and improvement suggestions for both the code and the agent loop that produced it."</description>
<location>global</location>
</skill>

<skill>
<name>swift-concurrency-pro</name>
<description>Reviews Swift code for concurrency correctness, modern API usage, and common async/await pitfalls. Use when reading, writing, or reviewing Swift concurrency code.</description>
<location>global</location>
</skill>

<skill>
<name>swift-security-expert</name>
<description>Use when working with iOS/macOS Keychain Services (SecItem queries, kSecClass, OSStatus errors), biometric authentication (LAContext, Face ID, Touch ID), CryptoKit (AES-GCM, ChaChaPoly, ECDSA, ECDH, HPKE, ML-KEM), Secure Enclave, secure credential storage (OAuth tokens, API keys), certificate pinning (SecTrust, SPKI), keychain sharing across apps/extensions, migrating secrets from UserDefaults or plists, or OWASP MASVS/MASTG mobile compliance on Apple platforms.</description>
<location>global</location>
</skill>

<skill>
<name>swift-testing-pro</name>
<description>Writes, reviews, and improves Swift Testing code using modern APIs and best practices. Use when reading, writing, or reviewing projects that use Swift Testing.</description>
<location>global</location>
</skill>

<skill>
<name>swiftdata-pro</name>
<description>Writes, reviews, and improves SwiftData code using modern APIs and best practices. Use when reading, writing, or reviewing projects that use SwiftData.</description>
<location>global</location>
</skill>

<skill>
<name>swiftui-design-principles</name>
<description>Design principles for building polished, native-feeling SwiftUI apps and widgets. Use this skill when creating or modifying SwiftUI views, iOS widgets (WidgetKit), or any native Apple UI. Ensures proper spacing, typography, colors, and widget implementations that look and feel like quality apps rather than AI-generated slop.</description>
<location>global</location>
</skill>

<skill>
<name>swiftui-pro</name>
<description>Comprehensively reviews SwiftUI code for best practices on modern APIs, maintainability, and performance. Use when reading, writing, or reviewing SwiftUI projects.</description>
<location>global</location>
</skill>

<skill>
<name>writing-for-interfaces</name>
<description>></description>
<location>global</location>
</skill>

</available_skills>
<!-- SKILLS_TABLE_END -->

</skills_system>
