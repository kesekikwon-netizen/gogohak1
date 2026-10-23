// commit-gate.mjs — Claude Code PreToolUse hook for the Bash and PowerShell tools.
//
// Blocks `git commit` until the build and a ctest run prove the staged change.
// CLAUDE.md only asks for "build and test before committing"; this hook makes
// it mechanical. A deny returns permissionDecision "deny" with a reason that
// names the missing evidence and the exact commands to run.
//
// Gate rule, applied only to commits whose content touches src/, tests/,
// cmake/, data/ or CMakeLists.txt (docs, .md, .claude/, scripts/ pass):
//   (1) build/Release/ka-hgis.exe (for tests/ files: the newest ka_*_tests.exe)
//       is newer than every such changed file, and a changed data/ file equals
//       the copy next to the exe when one exists;
//   (2) build/Testing/Temporary/LastTest.log was written after the newest exe
//       and after every changed file, is complete ("End testing"), lists at
//       least one test, and every listed test passed (no baseline failures are
//       excused since 2026-10-03: the former four pass locally and in CI).
// No build/ at all -> deny with the first-build command.
// Linux (2026-10-23): when build/Release does not exist, the same rule is applied to build-linux/ (ka-hgis
// and ka_*_tests without .exe next to build-linux/Testing/Temporary/LastTest.log). A commit that touches
// Windows-only code still needs the Windows CI run, which this hook cannot see.
//
// Line limit, for every commit (docs-only too): a C++ file under src/ or tests/ in the commit may
// pass 300 lines only when docs/quality/line-limit-baseline.txt lists it, and not past the listed
// count (the CI rule of scripts/scorecard.ps1 -LineLimitOnly). A commit that raises or adds a
// baseline entry needs a `길이 기준 변경: <이유>` line in its message.
//
// Evidence source: LastTest.log is rewritten by every ctest run, including a
// run that matched zero tests (then it has no "Test:" block, so zero tests
// never count as tested). LastTestsFailed.log is NOT used: ctest leaves a stale
// copy when a later run matches zero tests and deletes it when all tests pass.
//
// What counts as a commit and which files it contains:
//   - `git [opts] commit ...` (also via cd/-C, cmd /c, sh -c, powershell
//     -Command/-EncodedCommand, Invoke-Expression, $(...)), a git alias that
//     resolves to commit (git config or an inline `-c alias.X=commit`),
//     `git commit-tree`, and `git update-ref` of HEAD or refs/heads/*.
//     Files = staged changes (+ tracked worktree changes for -a/--include,
//     + the commit's own pathspecs).
//   - Staging earlier in the same chain (`git add x && git commit`) runs after
//     this hook, so the pathspecs of each such command are recorded and the
//     tracked changes and untracked files matching them are added. Only
//     `add -A/--all/-u/.`, `stage -A`, `add` with no pathspec, or commit -a
//     widen this to the whole worktree (merge, stash, am, apply, cherry-pick
//     and revert, which take no pathspecs, also widen).
//   - Script files: when a runner (bash/sh/node/python/powershell -File,
//     `. x.ps1`, source) gets, or the command word itself is, an existing
//     file under 1 MB (resolved against the effective cwd) whose text matches
//     git ... commit|commit-tree|update-ref, it is gated as a whole-worktree commit.
//   - Fallback: if nothing above matched but the raw text names
//     commit/commit-tree/update-ref together with git or an indirection
//     ($, eval, iex, xargs, &, alias), it is gated as a whole-worktree commit
//     in cwd. Such a deny still needs unverified build-relevant changes or a
//     line-limit problem, so a docs-only worktree passes unless it raises the
//     line-limit baseline.
//
// Fast path: a command with none of git/commit/update-ref, an encoded
// PowerShell command, eval/iex/xargs, or a script runner / script-like file
// name exits without spawning git. There is no skip flag, env var or marker file.
//
// Node ESM, no dependencies. Exit code is always 0; the decision is the JSON
// on stdout (no output = allow).

import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const BUILD_RELEVANT_DIRS = ['src/', 'tests/', 'cmake/', 'data/'];
const BUILD_RELEVANT_FILES = ['CMakeLists.txt'];

const CMD_FIRST_BUILD = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-now.ps1';
const CMD_BUILD =
  'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \'. ./scripts/dev-env.ps1; $ErrorActionPreference="Continue"; cmake --build build --config Release --parallel --target ka-hgis <test-target>; exit $LASTEXITCODE\'';
const CMD_BUILD_LOWMEM = '(C1060/MSB4018/MSB4166/0x800705AF 메모리 오류면 타깃 뒤에 `-- /m:1 /p:CL_MPCount=2` 를 붙여 다시 실행)';
const CMD_TEST =
  'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \'. ./scripts/dev-env.ps1; $ErrorActionPreference="Continue"; ctest --test-dir build -C Release -j4 --output-on-failure -R "^(name1|name2)$"; exit $LASTEXITCODE\'';

const CMD_FIRST_BUILD_LINUX = 'cmake --preset linux && cmake --build build-linux --parallel --target ka-hgis';
const CMD_BUILD_LINUX = 'cmake --build build-linux --parallel --target ka-hgis <test-target>';
const CMD_TEST_LINUX = 'ctest --test-dir build-linux -j4 --output-on-failure -R "^(name1|name2)$"';

// git subcommands that are not commits: no alias lookup needed for them.
const GIT_BUILTINS = new Set([
  'add', 'am', 'apply', 'archive', 'bisect', 'blame', 'branch', 'bugreport', 'bundle', 'cat-file', 'check-ignore',
  'checkout', 'cherry', 'cherry-pick', 'citool', 'clean', 'clone', 'commit-tree', 'config', 'count-objects',
  'credential', 'daemon', 'describe', 'diff', 'diff-tree', 'difftool', 'fetch', 'filter-branch', 'for-each-ref',
  'format-patch', 'fsck', 'gc', 'grep', 'gui', 'hash-object', 'help', 'init', 'instaweb', 'log', 'ls-files',
  'ls-remote', 'ls-tree', 'maintenance', 'merge', 'merge-base', 'merge-tree', 'mktag', 'mktree', 'mv', 'name-rev',
  'notes', 'pack-refs', 'prune', 'pull', 'push', 'range-diff', 'read-tree', 'rebase', 'reflog', 'remote', 'repack',
  'replace', 'request-pull', 'rerere', 'reset', 'restore', 'rev-list', 'rev-parse', 'revert', 'rm', 'send-email',
  'shortlog', 'show', 'show-ref', 'sparse-checkout', 'stash', 'status', 'submodule', 'switch', 'symbolic-ref', 'tag',
  'update-index', 'update-ref', 'var', 'version', 'whatchanged', 'worktree', 'write-tree',
]);

// ------------------------------------------------------------------------ io

function readStdin() {
  try {
    return JSON.parse(fs.readFileSync(0, 'utf8') || '{}');
  } catch {
    return {};
  }
}

function emitDeny(reason) {
  process.stdout.write(
    JSON.stringify({
      hookSpecificOutput: { hookEventName: 'PreToolUse', permissionDecision: 'deny', permissionDecisionReason: reason },
    }),
  );
}

function git(args, cwd) {
  return execFileSync('git', args, {
    cwd,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
    maxBuffer: 16 * 1024 * 1024,
  }).trim();
}
function tryGit(args, cwd) {
  try {
    return git(args, cwd);
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------- tokenizing

// Removes POSIX heredoc bodies (<<EOF ... EOF) so text inside them is not read as commands.
// The removed body lines go to `bodies` when given (a commit message fed by `-F -`).
function stripHeredocs(text, bodies) {
  const out = [];
  let terminator = null;
  let stripTabs = false;
  for (const line of text.split('\n')) {
    if (terminator !== null) {
      const probe = (stripTabs ? line.replace(/^\t+/, '') : line).replace(/\r$/, '');
      if (probe === terminator) terminator = null;
      else if (bodies) bodies.push(line);
      continue;
    }
    out.push(line);
    const m = line.match(/<<(-?)\s*(?:'([^']+)'|"([^"]+)"|\\?([A-Za-z_][\w-]*))/);
    if (m) {
      stripTabs = m[1] === '-';
      terminator = m[2] ?? m[3] ?? m[4];
    }
  }
  return out.join('\n');
}

// Splits a bash ('sh') or PowerShell ('ps') command string into simple commands
// (arrays of words). Handles '...', "...", backslash (sh) or backtick (ps)
// escapes, PowerShell here-strings, $(...) groups (also registered as their own
// command), comments, and the separators && || ; | & newline ( ) { }.
function splitCommands(text, flavor, depth = 0) {
  const cmds = [];
  let words = [];
  let word = '';
  let hasWord = false;
  const pushWord = () => {
    if (hasWord) words.push(word);
    word = '';
    hasWord = false;
  };
  const pushCmd = () => {
    pushWord();
    if (words.length) cmds.push(words);
    words = [];
  };
  const n = text.length;
  let i = 0;
  while (i < n) {
    const c = text[i];
    if (flavor === 'ps' && c === '@' && (text[i + 1] === "'" || text[i + 1] === '"') && /^\r?\n/.test(text.slice(i + 2, i + 4))) {
      const q = text[i + 1];
      const end = text.indexOf(`\n${q}@`, i + 2);
      word += end < 0 ? text.slice(i + 2) : text.slice(i + 2, end);
      hasWord = true;
      i = end < 0 ? n : end + 3;
      continue;
    }
    if (c === "'") {
      const end = text.indexOf("'", i + 1);
      word += end < 0 ? text.slice(i + 1) : text.slice(i + 1, end);
      hasWord = true;
      i = end < 0 ? n : end + 1;
      continue;
    }
    if (c === '"') {
      i++;
      let s = '';
      while (i < n && text[i] !== '"') {
        if (flavor === 'sh' && text[i] === '\\' && i + 1 < n && '"\\$`\n'.includes(text[i + 1])) {
          s += text[i + 1];
          i += 2;
          continue;
        }
        if (flavor === 'ps' && text[i] === '`' && i + 1 < n) {
          s += text[i + 1];
          i += 2;
          continue;
        }
        s += text[i++];
      }
      i++;
      word += s;
      hasWord = true;
      continue;
    }
    if (flavor === 'sh' && c === '\\' && i + 1 < n) {
      if (text[i + 1] !== '\n') {
        word += text[i + 1];
        hasWord = true;
      }
      i += 2;
      continue;
    }
    if (flavor === 'ps' && c === '`' && i + 1 < n) {
      if (text[i + 1] !== '\n' && text[i + 1] !== '\r') {
        word += text[i + 1];
        hasWord = true;
      }
      i += text[i + 1] === '\r' && text[i + 2] === '\n' ? 3 : 2;
      continue;
    }
    if (c === '$' && text[i + 1] === '(') {
      let level = 0;
      let j = i + 1;
      for (; j < n; j++) {
        if (text[j] === '(') level++;
        else if (text[j] === ')') {
          level--;
          if (level === 0) break;
        }
      }
      word += text.slice(i, j + 1);
      hasWord = true;
      if (depth < 4) for (const sub of splitCommands(text.slice(i + 2, j), flavor, depth + 1)) cmds.push(sub);
      i = j + 1;
      continue;
    }
    if (c === '#' && !hasWord) {
      const end = text.indexOf('\n', i);
      i = end < 0 ? n : end;
      continue;
    }
    if ('\n;|&(){}'.includes(c)) {
      pushCmd();
      i++;
      continue;
    }
    if (c === ' ' || c === '\t' || c === '\r') {
      pushWord();
      i++;
      continue;
    }
    word += c;
    hasWord = true;
    i++;
  }
  pushCmd();
  return cmds;
}

// ---------------------------------------------------------- command analysis

function isGitWord(w) {
  return /^(?:.*[\\/])?git(?:\.exe)?$/i.test(w);
}

// Staging commands whose non-option arguments are pathspecs.
const PATH_STAGING_SUBS = new Set(['add', 'stage', 'rm', 'mv', 'update-index', 'checkout', 'restore', 'reset']);
// Index-changing commands without pathspecs: a following commit covers the whole worktree.
const WIDE_STAGING_SUBS = new Set(['apply', 'am', 'cherry-pick', 'merge', 'revert', 'stash']);

function wholeWorktreeForm() {
  return { amend: false, all: true, untracked: true, pathspecs: [], help: false, stageGroups: [] };
}

const SCRIPT_RE = /\bgit\b[\s\S]*\b(?:commit|commit-tree|update-ref)\b/i;
const RUNNER_RE = /^(?:sh|bash|zsh|dash|ksh|busybox|node|python|python3|py|perl|ruby|powershell|pwsh|source|\.|cmd|call|start)(?:\.exe)?$/i;

// An existing regular file under 1 MB whose text runs a git commit.
function scriptCommits(word, dir) {
  if (!word || word.startsWith('-') || word.length > 400) return false;
  try {
    const p = path.resolve(dir, word.replace(/^~(?=$|[\\/])/, process.env.USERPROFILE || process.env.HOME || '~'));
    const st = fs.statSync(p);
    if (!st.isFile() || st.size >= 1024 * 1024) return false;
    return SCRIPT_RE.test(fs.readFileSync(p, 'latin1'));
  } catch {
    return false;
  }
}

// args of add/rm/mv/... -> { all, specs }
function stagingSpecs(sub, args) {
  const specs = [];
  let all = false;
  let onlyPaths = false;
  const isAdd = sub === 'add' || sub === 'stage';
  for (const a of args) {
    if (onlyPaths) {
      specs.push(a);
      continue;
    }
    if (a === '--') {
      onlyPaths = true;
      continue;
    }
    if (isAdd && (a === '--all' || a === '--update' || /^-[a-zA-Z]*[Au][a-zA-Z]*$/.test(a))) all = true;
    if (a.startsWith('-')) continue;
    specs.push(a);
  }
  if (specs.some((x) => x === '.' || x === './' || x === ':/' || x === ':' || x === '*')) all = true;
  if (isAdd && !specs.length) all = true;
  return { all, specs };
}

const CD_WORDS = new Set(['cd', 'pushd', 'chdir', 'set-location', 'sl', 'push-location']);

// `git [global options] <subcommand> [args]` -> { cPaths, sub, args } or null.
function parseGitInvocation(words) {
  let i = 1;
  const cPaths = [];
  const aliases = {};
  const noteAlias = (kv) => {
    const m = /^alias\.([^=]+)=([\s\S]*)$/i.exec(kv || '');
    if (m) aliases[m[1]] = m[2];
  };
  while (i < words.length) {
    const w = words[i];
    if (w === '-C') {
      cPaths.push(words[i + 1] ?? '');
      i += 2;
    } else if (/^-C./.test(w)) {
      cPaths.push(w.slice(2));
      i++;
    } else if (w === '-c') {
      noteAlias(words[i + 1]);
      i += 2;
    } else if (/^-c./.test(w)) {
      noteAlias(w.slice(2));
      i++;
    } else if (['-c', '--git-dir', '--work-tree', '--namespace', '--exec-path', '--config-env', '--super-prefix'].includes(w)) {
      i += 2;
    } else if (/^-c./.test(w) || w.startsWith('--') || /^-[pP]$/.test(w)) {
      i++; // --git-dir=X, --no-pager, --literal-pathspecs, --% ...
    } else break;
  }
  if (i >= words.length) return null;
  return { cPaths, aliases, sub: words[i], args: words.slice(i + 1) };
}

// commit options that take a separate value
const COMMIT_VALUE_OPTS = new Set([
  '-m', '--message', '-F', '--file', '-C', '--reuse-message', '-c', '--reedit-message', '--author', '--date',
  '-t', '--template', '--fixup', '--squash', '--cleanup', '--trailer', '--pathspec-from-file',
]);

function commitFormFromArgs(args) {
  const form = { amend: false, all: false, untracked: false, pathspecs: [], help: false, stageGroups: [] };
  let onlyPaths = false;
  for (let k = 0; k < args.length; k++) {
    const a = args[k];
    if (onlyPaths) {
      form.pathspecs.push(a);
      continue;
    }
    if (a === '--') {
      onlyPaths = true;
      continue;
    }
    if (COMMIT_VALUE_OPTS.has(a)) {
      k++;
      continue;
    }
    if (a === '--amend') form.amend = true;
    else if (a === '--all' || a === '--include') form.all = true;
    else if (a === '--help') form.help = true;
    else if (/^-[A-Za-z]+$/.test(a)) {
      // combined short flags such as -am, -asm; m F C c t consume the rest or the next token
      for (let j = 1; j < a.length; j++) {
        const ch = a[j];
        if (ch === 'a' || ch === 'i') form.all = true;
        else if (ch === 'h') form.help = true;
        else if ('mFCct'.includes(ch)) {
          if (j === a.length - 1) k++;
          break;
        }
      }
    } else if (a.startsWith('-')) continue;
    else form.pathspecs.push(a);
  }
  return form;
}

// update-ref whose ref is HEAD, refs/heads/* or a bare branch name (refs/snapshots/* etc. pass).
function updatesBranch(args) {
  const ref = args.find((a) => !a.startsWith('-'));
  if (ref === undefined) return args.includes('--stdin');
  return ref === 'HEAD' || ref.startsWith('refs/heads/') || !ref.startsWith('refs/');
}

function aliasIsCommit(value) {
  if (!value) return false;
  if (/^commit(?:\s|$)/.test(value)) return true;
  return value.startsWith('!') && /\bgit(?:\.exe)?\b[^;&|]*\bcommit\b/.test(value);
}

// Finds commit invocations in a command string: [{ dir, form, note }].
// Tracks `cd` inside the chain and recurses into cmd /c, sh -c, powershell
// -Command / -EncodedCommand, Invoke-Expression, and $(...) groups.
function findCommits(text, flavor, startDir, depth = 0, ctx = {}) {
  const found = [];
  if (depth > 4 || !text) return found;
  const body = flavor === 'sh' ? stripHeredocs(text) : text;
  let dir = startDir;
  // A chain such as `git add x && git commit` stages files AFTER this hook runs,
  // so the pathspecs of earlier staging commands are added to the commit.
  let stageAll = false;
  const stageGroups = [];
  for (const raw of splitCommands(body, flavor)) {
    let words = raw;
    while (words.length && /^[A-Za-z_][A-Za-z0-9_]*=/.test(words[0])) words = words.slice(1);
    while (words.length && /^(?:env|exec|command|builtin|nohup|time|&)$/i.test(words[0])) words = words.slice(1);
    if (!words.length) continue;
    const w0 = words[0].toLowerCase();

    // Script files run by a runner, or named as the command itself.
    if (RUNNER_RE.test(w0) || /[\\/.]/.test(words[0])) {
      const cand = RUNNER_RE.test(w0) ? words : [words[0]];
      const hit = cand.find((w) => scriptCommits(w, dir));
      if (hit) {
        found.push({ dir, form: wholeWorktreeForm(), note: `스크립트 ${hit} 안의 git commit 으로 본다` });
        continue;
      }
    }

    if (CD_WORDS.has(w0)) {
      let target = words.slice(1).find((w) => !w.startsWith('-')) ?? '';
      if (target === '' || target === '-') continue;
      target = target.replace(/^~(?=$|[\\/])/, process.env.USERPROFILE || process.env.HOME || '~');
      dir = path.resolve(dir, target);
      continue;
    }
    if (/^cmd(?:\.exe)?$/.test(w0)) {
      const k = words.findIndex((w, idx) => idx > 0 && /^\/[ck]$/i.test(w));
      if (k >= 0) found.push(...findCommits(words.slice(k + 1).join(' '), 'sh', dir, depth + 1, ctx));
      continue;
    }
    if (/^(?:sh|bash|zsh|dash|ksh|busybox)(?:\.exe)?$/.test(w0)) {
      const k = words.findIndex((w, idx) => idx > 0 && /^-[a-z]*c[a-z]*$/.test(w));
      if (k >= 0 && words[k + 1] !== undefined) found.push(...findCommits(words[k + 1], 'sh', dir, depth + 1, ctx));
      continue;
    }
    if (/^(?:powershell|pwsh)(?:\.exe)?$/.test(w0)) {
      for (let k = 1; k < words.length; k++) {
        const w = words[k].toLowerCase();
        if (/^-(?:c|com|comm|comma|comman|command)$/.test(w) && words[k + 1] !== undefined) {
          found.push(...findCommits(words.slice(k + 1).join(' '), 'ps', dir, depth + 1, ctx));
          break;
        }
        if (/^-(?:e|ec|en|enc|enco|encod|encode|encoded|encodedc|encodedcommand)$/.test(w) && words[k + 1] !== undefined) {
          let decoded = '';
          try {
            decoded = Buffer.from(words[k + 1], 'base64').toString('utf16le');
          } catch {}
          found.push(...findCommits(decoded, 'ps', dir, depth + 1, ctx));
          break;
        }
      }
      continue;
    }
    if (/^(?:invoke-expression|iex)$/.test(w0)) {
      found.push(...findCommits(words.slice(1).join(' '), 'ps', dir, depth + 1, ctx));
      continue;
    }
    if (w0 === 'start-process') {
      const joined = words.slice(1).join(' ');
      if (/\bgit(?:\.exe)?\b/i.test(joined) && /\bcommit\b/i.test(joined)) {
        found.push({ dir, form: commitFormFromArgs(words.slice(1)), note: 'Start-Process git ... commit' });
      }
      continue;
    }
    if (/^(?:node|python|python3|py|perl|ruby)(?:\.exe)?$/.test(w0)) {
      const joined = words.slice(1).join(' ');
      if (/\bgit(?:\.exe)?\b[\s\S]*\bcommit\b/i.test(joined)) {
        found.push({ dir, form: commitFormFromArgs([]), note: `${words[0]} 스크립트 안의 git commit 으로 본다` });
      }
      continue;
    }

    if (!isGitWord(words[0])) continue;
    const inv = parseGitInvocation(words);
    if (!inv) continue;
    let gitDir = dir;
    for (const p of inv.cPaths) gitDir = path.resolve(gitDir, p);
    const sub = inv.sub;
    if (sub === 'commit' || sub === 'commit-tree' || sub === 'update-ref') ctx.parsed = true;
    if (/^\$|\$\(/.test(sub)) {
      // `git $cmd`: the subcommand is only known at run time
      found.push({ dir: gitDir, form: wholeWorktreeForm(), note: `git ${sub} (실행할 때 정해지는 하위 명령)` });
      continue;
    }
    if (sub === 'commit-tree' || (sub === 'update-ref' && updatesBranch(inv.args))) {
      found.push({ dir: gitDir, form: wholeWorktreeForm(), note: `git ${sub} (plumbing commit)` });
      continue;
    }
    if (sub !== 'commit' && !GIT_BUILTINS.has(sub) && /^[A-Za-z][\w.-]*$/.test(sub)) {
      const alias = sub in inv.aliases ? inv.aliases[sub] : tryGit(['config', '--get', `alias.${sub}`], fs.existsSync(gitDir) ? gitDir : startDir);
      if (aliasIsCommit(alias)) found.push({ dir: gitDir, form: commitFormFromArgs(inv.args), note: `git alias "${sub}" = ${alias}` });
      continue;
    }
    if (WIDE_STAGING_SUBS.has(sub)) {
      stageAll = true;
      continue;
    }
    if (PATH_STAGING_SUBS.has(sub)) {
      const st = stagingSpecs(sub, inv.args);
      if (st.all) stageAll = true;
      else if (st.specs.length) stageGroups.push({ dir: gitDir, specs: st.specs });
      continue;
    }
    if (sub !== 'commit') continue;
    const form = commitFormFromArgs(inv.args);
    if (stageAll) {
      form.all = true;
      form.untracked = true;
    } else form.stageGroups = stageGroups.slice();
    if (form.help) continue;
    found.push({ dir: gitDir, form, note: '' });
  }
  return found;
}

// ------------------------------------------------------------------ evidence

function isBuildRelevant(rel) {
  const p = rel.replace(/\\/g, '/');
  if (BUILD_RELEVANT_FILES.includes(p) || BUILD_RELEVANT_FILES.includes(path.posix.basename(p))) return true;
  return BUILD_RELEVANT_DIRS.some((d) => p.startsWith(d));
}

function statMs(p) {
  try {
    return fs.statSync(p).mtimeMs;
  } catch {
    return null;
  }
}

function fmt(ms) {
  if (ms === null || ms === undefined) return '없음';
  const d = new Date(ms);
  const pad = (x) => String(x).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

// The commit the new one is compared with (HEAD, or HEAD~1 for --amend); null when there is none.
function commitBase(top, form) {
  const base = form.amend ? 'HEAD~1' : 'HEAD';
  return tryGit(['rev-parse', '--verify', '--quiet', base], top) !== null ? base : null;
}

// Files the commit will contain, relative to the repo top.
function changedFiles(top, form) {
  const base = commitBase(top, form);
  const hasBase = base !== null;
  const set = new Set();
  const add = (out) => {
    if (out === null) return;
    for (const f of out.split('\n')) if (f.trim()) set.add(f.trim());
  };
  add(hasBase ? tryGit(['diff', '--cached', '--no-renames', '--name-only', base], top) : tryGit(['diff', '--cached', '--no-renames', '--name-only'], top));
  if (form.all) add(tryGit(['diff', '--no-renames', '--name-only'], top)); // -a / --include: tracked worktree changes too
  if (form.untracked) add(tryGit(['ls-files', '--others', '--exclude-standard'], top)); // git add -A before commit in one chain
  for (const g of form.stageGroups || []) {
    // `git add <specs>` earlier in the chain: only what those pathspecs match (repo-top-relative output)
    const d = fs.existsSync(g.dir) ? g.dir : top;
    add(tryGit(['diff', '--no-renames', '--name-only', '--', ...g.specs], d));
    add(tryGit(['ls-files', '--full-name', '--others', '--exclude-standard', '--', ...g.specs], d));
  }
  if (form.pathspecs.length) add(tryGit(['diff', '--no-renames', '--name-only', ...(hasBase ? [base] : []), '--', ...form.pathspecs], top));
  return [...set];
}

// Files' content in the commit -> Map(file -> text): the index for a plain commit, the work tree when
// -a, pathspecs or an earlier `git add` in the chain take it from there. Untrimmed, so blank lines
// still count; null when the commit deletes the file. The index is read by one `git cat-file --batch`
// (a git per file made big commits run into the hook timeout, which lets the commit through), and a
// read that fails for any other reason throws, so the gate blocks instead of skipping the file.
function committedTexts(top, form, files) {
  const texts = new Map();
  if (form.all || form.untracked || form.pathspecs.length || (form.stageGroups || []).length) {
    for (const f of files) {
      try {
        texts.set(f, fs.readFileSync(path.join(top, f), 'utf8'));
      } catch (e) {
        if (e.code !== 'ENOENT') throw e;
        texts.set(f, null);
      }
    }
    return texts;
  }
  if (!files.length) return texts;
  const out = execFileSync('git', ['cat-file', '--batch'], {
    cwd: top,
    input: files.map((f) => `:${f}\n`).join(''),
    windowsHide: true,
    maxBuffer: 512 * 1024 * 1024,
  });
  let at = 0;
  for (const f of files) {
    const end = out.indexOf(10, at);
    if (end < 0) throw new Error(`git cat-file 이 ${f} 를 읽지 못했다 (출력이 끊겼다)`);
    const header = out.toString('utf8', at, end);
    at = end + 1;
    if (header === `:${f} missing`) {
      texts.set(f, null);
      continue;
    }
    const m = /^[0-9a-f]+ blob (\d+)$/.exec(header);
    if (!m) throw new Error(`git cat-file 이 ${f} 를 읽지 못했다: ${header}`);
    texts.set(f, out.toString('utf8', at, at + Number(m[1])));
    at += Number(m[1]) + 1;
  }
  return texts;
}
function committedText(top, form, f) {
  return committedTexts(top, form, [f]).get(f);
}

// Test lock (user decision 2026-10-03, docs/intent/2026-10-03-dev-setup-gaps.md): a commit that
// weakens a test that already exists needs a `시험 변경: <이유>` line in its message.
const ASSERT_RE = /\b(?:QCOMPARE|QVERIFY2?|QTRY_COMPARE(?:_WITH_TIMEOUT)?|QTRY_VERIFY2?(?:_WITH_TIMEOUT)?|QFAIL|QVERIFY_THROWS_\w+|QTest::newRow|QTest::addRow)\s*\(/g;
const SKIP_RE = /\b(?:QSKIP|QEXPECT_FAIL)\s*\(/g;
const REGISTER_RE = /\bka_add_qtest(?:_filter)?\s*\(/g;
const REASON_RE = /^\s*시험 변경\s*:\s*(?!없음\s*$)\S/m;

// Whole statements (macro to its closing parenthesis; a data row to its `;`), whitespace collapsed,
// so an assertion spread over several lines compares as one.
function statements(text, re) {
  const out = [];
  for (const m of text.matchAll(re)) {
    const toSemicolon = /newRow|addRow/.test(m[0]);
    let depth = 1;
    let i = m.index + m[0].length;
    for (; i < text.length; i++) {
      const c = text[i];
      if (c === '"' || c === "'") {
        for (i++; i < text.length && text[i] !== c; i++) if (text[i] === '\\') i++;
      } else if (c === '(') depth++;
      else if (c === ')' && --depth === 0 && !toSemicolon) break;
      else if (c === ';' && depth === 0) break;
    }
    out.push({ text: text.slice(m.index, i + 1).replace(/\s+/g, ' ').trim(), at: m.index });
  }
  return out;
}

// Name of the `void name(` test function a position is in, or null.
function enclosingFunction(text, at) {
  let name = null;
  for (const m of text.slice(0, at).matchAll(/\bvoid\s+(\w+)\s*\(/g)) name = m[1];
  return name;
}

// What the test files and CMakeLists.txt registrations lose against the base commit. All test files
// are compared together, so moving an assertion or renaming a file is not weakening; a skip inside
// a test function the base did not have belongs to a new test.
function weakenedTests(top, form, files) {
  const base = commitBase(top, form);
  const targets = files.filter((f) => f.startsWith('tests/') || path.posix.basename(f) === 'CMakeLists.txt');
  if (base === null || !targets.length) return [];
  const before = (f) => tryGit(['show', `${base}:${f}`], top);
  const after = (f) => committedText(top, form, f);
  const count = (list) => list.reduce((m, s) => m.set(s, (m.get(s) || 0) + 1), new Map());
  const take = (m, s) => (m.get(s) > 0 ? (m.set(s, m.get(s) - 1), true) : false);
  const findings = [];

  const names = (text) => statements(text || '', REGISTER_RE).flatMap((s) => s.text.slice(s.text.indexOf('(') + 1, -1).split(/\s+/).filter(Boolean));
  for (const f of targets.filter((t) => path.posix.basename(t) === 'CMakeLists.txt')) {
    const now = count(names(after(f)));
    for (const name of names(before(f))) if (!take(now, name)) findings.push(`${f}: 시험 등록에서 빠짐 \`${name}\``);
  }

  const code = targets.filter((t) => t.startsWith('tests/'));
  const old = code.map((f) => ({ f, text: before(f) || '' }));
  const neu = code.map((f) => ({ f, text: after(f) || '' }));
  const oldFunctions = new Set(old.flatMap(({ text }) => [...text.matchAll(/\bvoid\s+(\w+)\s*\(/g)].map((m) => m[1])));
  const nowAsserts = count(neu.flatMap(({ text }) => statements(text, ASSERT_RE).map((s) => s.text)));
  for (const { f, text } of old) for (const s of statements(text, ASSERT_RE)) if (!take(nowAsserts, s.text)) findings.push(`${f}: 지우거나 바꿈 \`${s.text}\``);
  const oldSkips = count(old.flatMap(({ text }) => statements(text, SKIP_RE).map((s) => s.text)));
  for (const { f, text } of neu) {
    for (const s of statements(text, SKIP_RE)) {
      if (take(oldSkips, s.text)) continue;
      const fn = enclosingFunction(text, s.at);
      if (fn && !oldFunctions.has(fn)) continue;
      findings.push(`${f}: 건너뛰게 함 \`${s.text}\``);
    }
  }
  return findings;
}

// Line limit (user decision 2026-10-03, docs/intent/2026-10-03-line-limit-precommit.md): the CI rule
// of scripts/scorecard.ps1 -LineLimitOnly for the C++ files in the commit, and a reason line for
// raising the baseline, like the test lock.
const LINE_LIMIT = 300;
const LINE_LIMIT_BASELINE = 'docs/quality/line-limit-baseline.txt';
const LIMIT_REASON_RE = /^\s*길이 기준 변경\s*:\s*(?!없음\s*$)\S/m;

// Lines as Get-Content counts them in scorecard.ps1: CRLF, LF or CR ends a line, and a final line
// end does not start another one.
function countLines(text) {
  const lines = text.split(/\r\n|\r|\n/);
  if (lines[lines.length - 1] === '') lines.pop();
  return lines.length;
}

// "<count> <path>" lines of the baseline -> Map(path -> count); # comments do not match.
function parseBaseline(text) {
  const map = new Map();
  for (const line of (text || '').split(/\r\n|\r|\n/)) {
    const m = /^\s*(\d+)\s+(\S.*)$/.exec(line);
    if (m) map.set(m[2].trim(), Number(m[1]));
  }
  return map;
}

function lineLimitProblems(top, form, files, message) {
  const base = commitBase(top, form);
  const baseBaseline = () => (base === null ? null : tryGit(['show', `${base}:${LINE_LIMIT_BASELINE}`], top));
  const baselineChanged = files.includes(LINE_LIMIT_BASELINE);
  const allowed = parseBaseline(baselineChanged ? committedText(top, form, LINE_LIMIT_BASELINE) : baseBaseline());
  const old = baselineChanged ? parseBaseline(baseBaseline()) : allowed;
  // A lowered or dropped entry (or a deleted baseline) is checked too, though its file is not in the commit.
  const lowered = [...old].filter(([f, lines]) => !allowed.has(f) || allowed.get(f) < lines).map(([f]) => f);
  const cpp = [...new Set([...files, ...lowered])].filter((x) => /^(?:src|tests)\/.+\.(?:cpp|h|hpp)$/i.test(x));
  const texts = committedTexts(top, form, cpp);
  const problems = [];

  const over = [];
  for (const f of cpp) {
    const text = texts.get(f);
    if (text === null) continue; // deleted in this commit
    const lines = countLines(text);
    if (lines <= LINE_LIMIT) continue;
    if (!allowed.has(f)) over.push(`${f}: ${lines}줄 (기준 파일에 없는 파일은 ${LINE_LIMIT}줄까지)`);
    else if (lines > allowed.get(f)) over.push(`${f}: ${lines}줄, 기준 ${allowed.get(f)}줄보다 많다`);
  }
  if (over.length) {
    problems.push(
      `C++ 파일이 길이 제한을 넘는다 (GitHub 검사 「Source line limit」와 같은 기준):\n    - ${over.join('\n    - ')}\n` +
        `  파일을 줄이거나 나눈다. 꼭 늘려야 하면 ${LINE_LIMIT_BASELINE} 의 줄 수를 올리고(새 파일은 줄을 더하고) 커밋 메시지에 \`길이 기준 변경: <이유>\` 줄을 넣는다.`,
    );
  }

  if (baselineChanged && !LIMIT_REASON_RE.test(message)) {
    const raised = [];
    for (const [f, lines] of allowed) {
      if (!old.has(f)) raised.push(`${f}: ${lines}줄로 새로 넣음`);
      else if (lines > old.get(f)) raised.push(`${f}: ${old.get(f)} → ${lines}줄`);
    }
    if (raised.length) {
      problems.push(
        `${LINE_LIMIT_BASELINE} 에서 줄 수를 올리거나 파일을 새로 넣었다:\n    - ${raised.join('\n    - ')}\n` +
          '  정말 필요하면 커밋 메시지에 `길이 기준 변경: <이유>` 줄을 넣는다. 아니면 기준이 아니라 파일을 줄인다.',
      );
    }
  }
  return problems;
}

// The commit message: -m/--message values, heredoc bodies (`-F -`), a -F/--file file, and for an
// --amend without a new message the message being amended.
function messageText(command, cwd, top, form) {
  const parts = [];
  stripHeredocs(command, parts);
  for (const m of command.matchAll(/(?:^|\s)(?:-[a-zA-Z]*m|--message=?)\s*("(?:[^"\\]|\\.)*"|'[^']*'|[^\s;&|]+)/g)) {
    parts.push(m[1].replace(/^(["'])([\s\S]*)\1$/, '$2'));
  }
  const files = [...command.matchAll(/(?:^|\s)(?:-F|--file)(?:=|\s+)("[^"]+"|'[^']+'|[^\s;&|]+)/g)];
  for (const m of files) {
    const name = m[1].replace(/^["']|["']$/g, '');
    if (name === '-') continue;
    try {
      parts.push(fs.readFileSync(path.resolve(cwd, name), 'utf8'));
    } catch {}
  }
  if (form.amend && !files.length && !/(?:^|\s)(?:-[a-zA-Z]*m|--message)/.test(command)) {
    parts.push(tryGit(['log', '-1', '--format=%B'], top) || '');
  }
  return parts.join('\n');
}

// ctest LastTest.log -> { complete, tests: [{ name, result }] }
function parseLastTestLog(text) {
  const tests = [];
  let pending = null;
  for (const line of text.split('\n')) {
    const l = line.replace(/\r$/, '');
    const m = l.match(/^\d+\/\d+ Test: (.+)$/);
    if (m) {
      pending = { name: m[1].trim(), result: 'unknown' };
      tests.push(pending);
      continue;
    }
    const r = l.match(/^Test (Passed|Failed|Timeout|Not Run|Exception|Skipped|Disabled)\.?$/i) || l.match(/^\*\*\*(Failed|Timeout|Not Run|Exception|Skipped)/i);
    if (r && pending) {
      pending.result = r[1].toLowerCase();
      pending = null;
    }
  }
  return { complete: /^End testing:/m.test(text), tests };
}

function newest(list) {
  return list.reduce((a, b) => (a === null || b.ms > a.ms ? b : a), null);
}

function evaluate(top, form, files) {
  const problems = [];
  const relevant = files.filter(isBuildRelevant);
  if (!relevant.length) return { gated: false, files };

  const winDir = path.join(top, 'build');
  const linuxDir = path.join(top, 'build-linux');
  const useWindows = fs.existsSync(path.join(winDir, 'CMakeCache.txt')) && fs.existsSync(path.join(winDir, 'Release'));
  const useLinux = !useWindows && fs.existsSync(path.join(linuxDir, 'CMakeCache.txt'));
  if (!useWindows && !useLinux) {
    return {
      gated: true,
      files: relevant,
      problems: [`build/ 가 없다 (${winDir}). 첫 빌드 (Windows 약 8분):\n    ${CMD_FIRST_BUILD}\n  리눅스는 build-linux/:\n    ${CMD_FIRST_BUILD_LINUX}`],
    };
  }
  const buildDir = useLinux ? linuxDir : winDir;
  const release = useLinux ? linuxDir : path.join(winDir, 'Release');
  const label = useLinux ? 'build-linux' : 'build';
  const appName = useLinux ? 'ka-hgis' : 'ka-hgis.exe';
  const appLabel = useLinux ? 'build-linux/ka-hgis' : 'build/Release/ka-hgis.exe';
  const isExe = useLinux ? (f) => /^ka(?:-hgis|_.*_tests)$/.test(f) : (f) => /\.exe$/i.test(f);
  const isTestExe = useLinux ? (f) => /^ka_.*_tests$/.test(f) : (f) => /^ka_.*_tests\.exe$/i.test(f);
  const buildHint = useLinux ? `    ${CMD_BUILD_LINUX}` : `    ${CMD_BUILD}\n    ${CMD_BUILD_LOWMEM}`;
  const cmdTest = useLinux ? CMD_TEST_LINUX : CMD_TEST;

  // (1) build output newer than every changed build-relevant file
  const appMs = statMs(path.join(release, appName));
  let exes = [];
  try {
    exes = fs
      .readdirSync(release)
      .filter(isExe)
      .map((f) => ({ name: f, ms: statMs(path.join(release, f)) }))
      .filter((e) => e.ms !== null);
  } catch {}
  const newestExe = newest(exes);
  const newestTestExe = newest(exes.filter((e) => isTestExe(e.name)));
  if (!newestExe) {
    return { gated: true, files: relevant, problems: [`${useLinux ? 'build-linux 에 ka-hgis 가' : 'build/Release 에 .exe 가'} 없다. 빌드:\n${buildHint}`] };
  }

  let newestChangedMs = 0;
  const stale = [];
  for (const rel of relevant) {
    const abs = path.join(top, rel);
    const ms = statMs(abs);
    if (ms === null) continue; // deleted in this commit: nothing to compare
    newestChangedMs = Math.max(newestChangedMs, ms);
    const p = rel.replace(/\\/g, '/');
    if (p.startsWith('tests/')) {
      if (!newestTestExe || newestTestExe.ms <= ms) {
        stale.push(`${rel} (${fmt(ms)}) 가 최신 테스트 exe ${newestTestExe ? `${newestTestExe.name} (${fmt(newestTestExe.ms)})` : '(없음)'} 보다 새롭다`);
      }
      continue;
    }
    if (appMs === null || appMs <= ms) {
      stale.push(`${rel} (${fmt(ms)}) 가 ${appLabel} (${fmt(appMs)}) 보다 새롭다`);
      continue;
    }
    if (p.startsWith('data/')) {
      const copy = path.join(release, p);
      if (fs.existsSync(copy)) {
        try {
          if (!fs.readFileSync(abs).equals(fs.readFileSync(copy))) stale.push(`${rel} 의 내용이 ${label}${useLinux ? '' : '/Release'}/${p} 와 다르다 (빌드가 아직 복사하지 않았다)`);
        } catch (e) {
          stale.push(`${rel}: 복사본 비교 실패 (${e.message})`);
        }
      }
    }
  }
  if (stale.length) {
    problems.push(`빌드 결과가 바뀐 파일보다 오래됐다:\n    - ${stale.join('\n    - ')}\n  빌드:\n${buildHint}`);
  }

  // (2) ctest after the build, at least one test, all passed
  const lastLog = path.join(buildDir, 'Testing', 'Temporary', 'LastTest.log');
  const logMs = statMs(lastLog);
  const testHint = `  테스트 (바꾼 영역의 그룹은 docs/testing-map.md):\n    ${cmdTest}`;
  if (logMs === null) {
    problems.push(`ctest 기록이 없다 (${label}/Testing/Temporary/LastTest.log).\n${testHint}`);
  } else if (logMs <= newestExe.ms) {
    problems.push(`마지막 ctest (${fmt(logMs)}) 가 최신 빌드 ${newestExe.name} (${fmt(newestExe.ms)}) 보다 먼저 돌았다. 빌드 뒤에 다시 테스트:\n${testHint}`);
  } else if (logMs <= newestChangedMs) {
    problems.push(`마지막 ctest (${fmt(logMs)}) 가 바뀐 파일 (${fmt(newestChangedMs)}) 보다 먼저 돌았다. 빌드와 테스트를 다시:\n${testHint}`);
  } else {
    let parsed;
    try {
      parsed = parseLastTestLog(fs.readFileSync(lastLog, 'latin1'));
    } catch {
      parsed = { complete: false, tests: [] };
    }
    if (!parsed.complete) {
      problems.push(`마지막 ctest 기록이 끝까지 쓰이지 않았다 ("End testing" 없음: 중단됐거나 아직 실행 중). 다시 테스트:\n${testHint}`);
    } else if (!parsed.tests.length) {
      problems.push(`마지막 ctest 가 테스트를 하나도 돌리지 않았다 (-R 이 아무 이름과도 안 맞음, "No tests were found"). 0개 실행은 검증이 아니다. 실제 이름으로 다시:\n${testHint}`);
    } else {
      const real = parsed.tests.filter((t) => t.result !== 'passed').map((t) => `${t.name} (${t.result})`);
      if (real.length) {
        problems.push(
          `마지막 ctest 에서 실패한 테스트가 있다:\n    - ${real.join('\n    - ')}\n  먼저 고치고, 빌드와 테스트를 다시 통과시킨 뒤 커밋한다. 결과: ${label}/test-logs/<name>.txt\n${testHint}`,
        );
      }
    }
  }
  return { gated: true, files: relevant, problems };
}

// ---------------------------------------------------------------------- main

function main() {
  const input = readStdin();
  if (input.hook_event_name && input.hook_event_name !== 'PreToolUse') return;
  const tool = String(input.tool_name || '');
  const command = input.tool_input && typeof input.tool_input.command === 'string' ? input.tool_input.command : '';
  if (!command) return;
  // fast path: no git, no commit word, no indirection and nothing that could be a script
  if (!/git|commit|update-ref|-enc|encodedcommand|eval|iex|invoke-expression|xargs|\.(?:sh|bash|ps1|psm1|js|mjs|cjs|py|pl|rb|cmd|bat)\b|\b(?:sh|bash|zsh|dash|ksh|busybox|node|python3?|py|perl|ruby|powershell|pwsh|source)\b|(?:^|[\s;&|(])\.\s/i.test(command)) return;

  const flavor = /powershell/i.test(tool) ? 'ps' : 'sh';
  const cwd = input.cwd || process.cwd();
  let commits;
  const ctx = {};
  try {
    commits = findCommits(command, flavor, cwd, 0, ctx);
  } catch (e) {
    emitDeny(`[commit-gate] 명령을 해석하지 못해 커밋을 막았다 (${e.message}). git commit 을 한 줄짜리 단순한 형태로 다시 실행한다.`);
    return;
  }
  if (!commits.length) {
    // Conservative fallback for forms the parser cannot follow (alias tricks, eval, xargs, variables).
    // ctx.parsed: a literal git commit/commit-tree/update-ref was read and judged
    // (commit --help, update-ref refs/snapshots/...); then only indirection reopens it.
    const indirect = /\$|\beval\b|\biex\b|invoke-expression|\bxargs\b|alias/i.test(command);
    if ((!ctx.parsed || indirect) && /\b(?:commit|commit-tree|update-ref)\b/i.test(command) && /\bgit\b|\$|\beval\b|\biex\b|invoke-expression|\bxargs\b|&|alias/i.test(command)) {
      commits = [{ dir: cwd, form: wholeWorktreeForm(), note: '해석하지 못한 형태의 commit 으로 보고 작업 트리 전체를 검사한다' }];
    } else return;
  }

  const reasons = [];
  for (const c of commits) {
    let top;
    try {
      if (!fs.existsSync(c.dir)) throw new Error('no such directory');
      top = path.resolve(git(['rev-parse', '--show-toplevel'], c.dir));
    } catch {
      continue; // not a repository: git itself refuses the commit
    }
    if (!fs.existsSync(path.join(top, 'CMakeLists.txt'))) continue; // not a CMake project: nothing to build
    const files = changedFiles(top, c.form);
    let r;
    try {
      r = evaluate(top, c.form, files);
    } catch (e) {
      r = { gated: true, files: [], problems: [`게이트 내부 오류: ${e.stack || e.message}`] };
    }
    const message = messageText(command, c.dir, top, c.form);
    if (r.gated && !REASON_RE.test(message)) {
      const weak = weakenedTests(top, c.form, r.files);
      if (weak.length) {
        const shownWeak = weak.slice(0, 8).join('\n    - ') + (weak.length > 8 ? `\n    - 외 ${weak.length - 8}개` : '');
        r.problems.push(
          `원래 있던 시험을 약하게 바꿨다 (기댓값을 바꾸거나 지우거나 건너뛰게 함):\n    - ${shownWeak}\n` +
            '  정말 바꿔야 하면 커밋 메시지에 `시험 변경: <이유>` 줄을 넣고, 보고 맨 위에 「바꾼 시험」으로 사용자에게 알린다. 아니면 시험이 아니라 코드를 고친다.',
        );
      }
    }
    const problems = [...(r.problems || []), ...lineLimitProblems(top, c.form, files, message)];
    if (!problems.length) continue;
    const shown = r.files.slice(0, 12).join(', ') + (r.files.length > 12 ? ` 외 ${r.files.length - 12}개` : '');
    reasons.push(
      `[commit-gate] 커밋 차단 (${top}${c.note ? `; ${c.note}` : ''}${c.form.amend ? '; --amend' : ''})\n` +
        (r.gated ? `빌드 대상 파일이 커밋에 들어 있다: ${shown}\n` : '') +
        `막은 이유:\n- ${problems.join('\n- ')}\n` +
        `모두 갖춘 뒤 같은 git commit 을 다시 실행한다. 이 게이트를 건너뛰는 옵션은 없다. 문서(.md)·.claude/·scripts/ 만 바꾼 커밋에는 빌드·테스트를 요구하지 않는다.`,
    );
  }
  if (reasons.length) emitDeny(reasons.join('\n\n'));
}

try {
  main();
} catch (e) {
  // A bug in the gate must not silently open it: deny and say what broke.
  emitDeny(`[commit-gate] 훅 자체가 실패했다: ${e && e.stack ? e.stack : e}`);
}
