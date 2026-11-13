# The commit and pull request standard

How Reluva's commits, branches and pull requests are written. CI checks
what it can (see [Checks](#checks)); the rest is for the author and the
reviewer. What may be used at all is in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Commit messages

### The subject

```text
<scope>: <what changed>
```

- **The scope** says where the change is. It's one of:
  - an engine's id, as `engines.cfg` has it, or `testbed`: `clue`,
    `idtech3`, `nfs`...;
  - a folder under `src/`: `common`, `graphics`, `audio`, `backends`...;
  - `docs`, `tools`, `tests`, `ci`, `build` or `third_party`.

  It may go on with a path, to be more precise: `common/x86`,
  `docs/DEVELOPMENT.md`, `tools/tidy.py`. A change that touches several
  areas takes the scope of the one it's about.
- **What changed**, in a few words, as the rest of the project writes:
  `clue: walks into a room take the way-finder`. No full stop at the end.
- **Length:** 72 characters or less where possible, never more than 100.
- **Merges and reverts** keep git's and GitHub's own subjects (`Merge pull
  request #12 from ...`, `Merge origin/main into ...`, `Revert "..."`).

### The body

- **Why, not only what.** The diff shows what changed; the body says why,
  and anything a reviewer needs to know: what was measured, what was
  ruled out, what's left.
- **Sources.** For a change that rests on reverse engineering or on
  watching the original, say where the facts came from: an address in a
  named binary, a file, a recording, a published write-up. The docs must
  say the same, on the fact's page
  ([Sources and confidence](DOCUMENTATION_STANDARD.md#sources-and-confidence)):
  nobody should need the commit history to find where a fact came from.
- **Wrapped at 72 characters**, but for lines that can't be: URLs, paths,
  commands, table rows.
- A blank line between the subject and the body, and before the trailers.

### Trailers

At the end of the message, one per line:

- **`Signed-off-by: Name <email>`** on every commit (`git commit -s` adds
  it). It means: *this change was written from the sources
  [CONTRIBUTING.md](../CONTRIBUTING.md) allows, and from nothing it
  forbids*. It's made with the committer's own name and address.
- **`Co-Authored-By: Name <email>`** for each other author, including an
  AI assistant that wrote part of the change. The person who signs off is
  answerable for all of it.

## What a commit holds

- **One logical change.** A fix, a feature, a rename: not several at once.
  A change that needs a preparatory refactor is two commits.
- **Every commit builds and passes the tests**, so `git bisect` can be
  used on any of them.
- **Mechanical changes stay apart from real ones.** A commit that only
  reformats, renames or moves, made by a script, holds nothing else; its
  hash goes in [.git-blame-ignore-revs](../.git-blame-ignore-revs) (that
  file says how) so `git blame` looks past it.
- **No game files**, ever: no data, executables, audio, video or text of a
  game, nor anything that would stand in for them. Tests build their own
  data.

## Branches

```text
<kind>/<scope>-<topic>
```

| Kind | For | Example |
|---|---|---|
| `engine` | An engine's own work | `engine/clue-network` |
| `fix` | A fix, anywhere | `fix/idtech3-md3-empty-surface` |
| `common` | Shared code under `src/` | `common/x86-sse` |
| `docs` | Docs only | `docs/terms` |
| `tools` | `tools/` and developer commands | `tools/check-commits` |
| `ci` | CI and the build | `ci/macos-runner` |

In lower case, with hyphens. Branches that Claude Code sessions make
(`claude/...`) keep the names they're given.

## Pull requests

- **The title** is written as a commit subject: `<scope>: <what
  changed>`. It becomes the merge commit's subject.
- **The description** follows the template
  ([.github/pull_request_template.md](../.github/pull_request_template.md)):
  what and why, sources, how it was tested, docs, and a short checklist.
- **One engine or one topic per pull request** where possible. A large
  mechanical change (formatting, a rename across the tree) is a pull
  request of its own, with nothing else in it.
- **Merged with a merge commit**, which keeps every commit of the branch.
- **Kept up to date by merging `main` into the branch.** A branch others
  may have checked out is never rebased or force-pushed; only its author's
  own unpublished commits may be.

## Checks

On every pull request, CI's "Commits" job runs

```sh
python3 tools/check-commits.py <base>..<head>
```

over the pull request's commits, and on its branch's name. It can be run
before pushing, with `origin/main..HEAD`. It checks:

- each subject's form: a known scope, something after it, no full stop,
  at most 100 characters;
- each body's lines: 72 characters at most, but for trailers, URLs and
  lines that can't be broken (no spaces);
- a `Signed-off-by:` trailer on each commit;
- the branch's name;
- that no file added or changed is a game's: none with a game data
  extension (`.pk3`, `.bik`, `.exe`, `.dll`...), and none over 1 MB, but
  for those the script lists (the render check's reference frames).

Merge commits are only checked for game files. What it can't check, the
template's checklist asks the author and the reviewer.
