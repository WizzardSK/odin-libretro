# Prints a .gitlab-ci.yml job as a shell script for the buildbot preflight:
# its variables, before_script and script, following its local extends (the
# templates' own parts are left to the preflight job, which stands in for
# their images). Extra arguments are shell lines put before the script.
import sys
import yaml

ci = yaml.safe_load(open('.gitlab-ci.yml'))


def resolve(name):
    job = ci.get(name, {})
    merged = {'variables': {}}
    extends = job.get('extends', [])
    for parent in [extends] if isinstance(extends, str) else extends:
        if parent in ci:
            base = resolve(parent)
            merged['variables'].update(base.pop('variables', {}))
            merged.update(base)
    merged['variables'].update(job.get('variables', {}))
    for key in ('before_script', 'script'):
        if key in job:
            merged[key] = job[key]
    return merged


job = resolve(sys.argv[1])
print('set -ex')
print('export CI_PROJECT_DIR=$PWD')
for key, value in job['variables'].items():
    print(f'export {key}={value}')
for line in sys.argv[2:]:
    print(line)
for line in job.get('before_script', ['export NUMPROC=$(($(nproc)/5)); [ "$NUMPROC" -ge 1 ] || NUMPROC=1']):
    print(line)
print('git config --global --add safe.directory "$PWD" 2>/dev/null || true')
for line in job['script']:
    print(line)
