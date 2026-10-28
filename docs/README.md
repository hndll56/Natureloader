# Documentation index

All public project documentation is being maintained in English. Use these documents as the source of truth:

- [AI agent guide](AI_AGENT_GUIDE.md): architecture, workflow, tests, and handoff.
- [Mod API](MOD_API.md): package format, schema, validation, and CLI.
- [Making mods](MEMBUAT_MOD.md): practical mod authoring workflow.
- [Content registry](MOD_CONTENT.md): item, NPC, map, building, tool, asset, hook, and script authoring.
- [Native item patches](NATIVE_ITEM_MODS.md): supported retail item fields and limits.
- [Native item model](NATIVE_ITEM_RUNTIME.md): modcore-only native item slice, id aliases, save layout, limits.
- [Migration](MOD_MIGRATION.md): migration, backups, and rollback limits.
- [Architecture](ARCHITECTURE_V2.md): unified modcore architecture.
- [Porting](PORTING.md): native runtime status and milestones.
- [Native progress](NATIVE_PROGRESS.md): verified runtime evidence and open issues.
- [Decomp integration](DECOMP_INTEGRATION.md): source and runtime integration notes.
- [ROM variants](ROM_VARIANTS.md): ROM and executable compatibility.
- [File review](FILES_REVIEW.md): provenance and repository filtering.

When a document conflicts with runtime tests, tests and explicit status notes win. Update the relevant document in the same commit as behavior changes.
