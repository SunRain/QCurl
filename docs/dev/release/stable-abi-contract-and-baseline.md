# Future TODO: stable ABI contract and baseline

Status: Deferred. This project is not a QCurl 2.0 release blocker.

## Current boundary

QCurl 2.0 provides a source-compatible Core contract and requires downstream consumers to rebuild and relink after every QCurl update. It does not promise binary compatibility between 2.x builds. `SOVERSION 2` is the current loader namespace, not a stable ABI guarantee.

Until this TODO is completed, public documentation, package metadata, and release evidence must not claim a stable ABI or require `abi/baseline/qcurl-core-v2.abi.xml`.

## Goal

Establish an explicit, testable ABI policy suitable for a public Qt6/KDE-style C++ library, then create the first controlled baseline from a final stable release identity.

## Required decisions

1. Define the public Core ABI scope and explicitly exclude private, test, and Preview surfaces.
2. Define the supported platform tuple: operating system, architecture, compiler, standard library ABI, Qt version range, build type, feature flags, and libcurl configuration.
3. Define how `SOVERSION` changes relate to compatible and incompatible ABI changes.
4. Choose the baseline storage and retention model: repository file, signed release asset, immutable tag-derived artifact, or CI artifact with an independent retention policy.
5. Define whether compatibility is guaranteed across patch releases, minor releases, or another documented interval.
6. Define downstream package metadata and rebuild policy so the loader cannot silently combine incompatible binaries.

## Implementation scope

- Write the stable ABI contract and platform support matrix.
- Generate the first baseline from a clean, tagged release using a fixed toolchain.
- Make the baseline reproducible from that tag and recorded build tuple.
- Promote `current` ABI comparison into a required release gate only after the baseline exists.
- Restore a controlled candidate/promotion/final workflow, or replace it with an equally strict immutable-artifact workflow.
- Bind ABI reports, snapshots, tool versions, library digests, and source identity into the release manifest.
- Add negative tests proving that missing, stale, cross-platform, or manually refreshed baselines cannot pass.
- Align CMake `SOVERSION`, package metadata, release procedure, migration guidance, and downstream compatibility wording.

## Acceptance criteria

- The stable ABI policy names its exact public scope and supported build tuple.
- A baseline can be regenerated from the recorded release tag with a documented, deterministic procedure.
- The release gate rejects unintended public ABI removal or change and identifies intentional additions.
- Baseline promotion cannot hide drift by regenerating the expected file from the candidate under test.
- Public docs, CMake metadata, release manifests, and package-manager rebuild rules express the same contract.
- A downstream binary-compatibility test runs against both the baseline release and the candidate.
- The first release covered by the stable ABI policy explicitly names the compatibility interval.

## Activation rule

Do not promote this TODO into the release gate until all acceptance criteria pass on one clean release candidate. If the supported platform tuple cannot be made reproducible, keep the source-compatible, rebuild-required contract and do not publish a partial ABI guarantee.
