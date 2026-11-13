<!-- Title: <scope>: <what changed>, as a commit subject
(docs/COMMIT_STANDARD.md). Delete these comments as you fill in. -->

## What and why

<!-- What this changes, and why. One engine or one topic: a large
mechanical change (formatting, a rename) is a pull request of its own. -->

## Sources

<!-- Where the facts this rests on came from: addresses in a named binary,
files, recordings, published write-ups. Each must be one CONTRIBUTING.md
allows. "None" for a change that rests on no facts about a game. -->

## How it was tested

<!-- The tests added or run, and anything checked by hand: on which
release and platform. -->

## Docs

<!-- The pages added or changed, or why none needed to be. -->

## Checklist

- [ ] Every commit is signed off (`git commit -s`), from allowed sources only
- [ ] No game files in any commit
- [ ] Each commit builds and passes the tests
- [ ] Mechanical commits are apart, and in `.git-blame-ignore-revs`
- [ ] The docs say where each new fact came from
- [ ] `python3 tools/check-docs.py` and `python3 tools/check-commits.py origin/main..HEAD` pass
