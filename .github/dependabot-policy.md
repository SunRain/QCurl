# Dependabot policy

## `tests/libcurl_consistency` Python lock

`tests/libcurl_consistency/requirements.lock.txt` is not a normal "track latest
PyPI" lock file. It mirrors upstream curl testenv dependencies from
`curl/tests/http/requirements.txt`, with QCurl-local Brotli support appended.

Dependabot cannot make version updates conditional on a different upstream
repository file changing. For that reason, `.github/dependabot.yml` disables
version-update pull requests for the `pip` ecosystem in
`/tests/libcurl_consistency`.

Update this lock file only when upstream curl changes
`tests/http/requirements.txt` and the delta has been reviewed for this test
suite. Direct PyPI-only bumps, such as `psutil` moving from one released version
to another without an upstream curl testenv change, are intentionally out of
scope.
