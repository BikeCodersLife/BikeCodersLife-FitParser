# Releasing fit-parser

The binary version is pinned in FOUR downstream places. Miss one and either a
host image silently builds with a stale binary, or a `FitParserPinDriftTest`
fails. Work through this list top to bottom for every release.

## 1. In this repo (before tagging)

- [ ] `src/main.cpp` — `printVersion()`: bump the headline + add a changelog line
- [ ] `CMakeLists.txt` — `project(fit-parser VERSION x.y.z ...)` (this one was
      forgotten for all of v2.1.x — hence this file)
- [ ] `tests/generate_expected.sh` if the JSON output changed, then
      `tests/run_tests.sh` → 97/97 (with the dev-local FIT fixtures; CI has fewer)
- [ ] Commit, then push + tag (push main first, tag second):

```bash
git push origin main
git tag -a vX.Y.Z -m "Release vX.Y.Z: <summary>"
git push origin vX.Y.Z          # triggers .github/workflows/release.yml
```

## 2. After CI publishes the release asset

Compute the sha of the published artifact:

```bash
curl -sL https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/download/vX.Y.Z/fit-parser-linux-amd64.tar.gz | sha256sum
```

Then bump, in this order (the pin file is the single authority; the Dockerfile
ARGs are fallback defaults that must mirror it):

- [ ] **IntegrationsBundle** `fit-parser.version` — `{"version": "vX.Y.Z", "sha256": "..."}`
- [ ] **IntegrationsBundle** `src/Common/Service/RideFileParser/FitParser.php`
      — `EXPECTED_BINARY_VERSION = 'vX.Y.Z'`
- [ ] **MaintenanceCore** `Dockerfile` — `ARG FIT_PARSER_VERSION` + `ARG FIT_PARSER_SHA256`
- [ ] **teneo-tessera** `Dockerfile.worker-compute` — `ARG FIT_PARSER_VERSION` + `ARG FIT_PARSER_SHA256`

## 3. Roll out

- [ ] Push the bundle; `composer update bikecoderslife/integrations-bundle` in
      each host (MC, tessera)
- [ ] Each host's `FitParserPinDriftTest` must pass (Dockerfile ↔ vendored pin)
- [ ] Rebuild + redeploy host images (MC php container, tessera worker-compute)
