// node --test .claude/hooks/commit-gate.test.mjs
// Each test builds a throwaway git repo with fake build and ctest evidence, stages a change and asks
// the hook about `git commit`. Only the test-weakening check and the line limit are covered here.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HOOK = path.join(path.dirname(fileURLToPath(import.meta.url)), 'commit-gate.mjs');

const TEST_CPP = [
  '#include <QtTest>',
  'class T : public QObject {',
  '  Q_OBJECT',
  'private slots:',
  '  void adds() {',
  '    QCOMPARE(1 + 1, 2);',
  '    QVERIFY(true);',
  '  }',
  '};',
  '',
].join('\n');
const CMAKE = 'project(x)\nka_add_qtest(adds ka_adds_tests)\nka_add_qtest_filter(save_open_x ka_adds_tests\n  adds\n  more)\n';
const BASELINE = 'docs/quality/line-limit-baseline.txt';
const linesOf = (n, eol = '\n') => `${Array.from({ length: n }, (_, i) => `// line ${i + 1}`).join(eol)}${eol}`;

function repoWith(t) {
  const repo = fs.mkdtempSync(path.join(os.tmpdir(), 'commit-gate-'));
  t.after(() => fs.rmSync(repo, { recursive: true, force: true }));
  const git = (...args) => execFileSync('git', ['-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'core.autocrlf=false', ...args], { cwd: repo, stdio: 'pipe' });
  fs.mkdirSync(path.join(repo, 'tests'));
  fs.writeFileSync(path.join(repo, 'CMakeLists.txt'), CMAKE);
  fs.writeFileSync(path.join(repo, 'tests', 'test_adds.cpp'), TEST_CPP);
  fs.mkdirSync(path.join(repo, 'src'));
  fs.writeFileSync(path.join(repo, 'src', 'big.cpp'), linesOf(310));
  fs.mkdirSync(path.join(repo, 'docs', 'quality'), { recursive: true });
  fs.writeFileSync(path.join(repo, BASELINE), '# C++ files already over the 300-line limit\n310 src/big.cpp\n');
  git('init', '-q');
  git('add', '-A');
  git('commit', '-q', '-m', 'base');
  return { repo, git };
}

// Build and ctest evidence newer than every change, so only the weakening check or the line limit can deny.
function evidence(repo) {
  const release = path.join(repo, 'build', 'Release');
  fs.mkdirSync(path.join(repo, 'build', 'Testing', 'Temporary'), { recursive: true });
  fs.mkdirSync(release, { recursive: true });
  fs.writeFileSync(path.join(repo, 'build', 'CMakeCache.txt'), '');
  const now = Date.now() / 1000;
  for (const exe of ['ka-hgis.exe', 'ka_adds_tests.exe']) {
    fs.writeFileSync(path.join(release, exe), '');
    fs.utimesSync(path.join(release, exe), now + 60, now + 60);
  }
  const log = path.join(repo, 'build', 'Testing', 'Temporary', 'LastTest.log');
  fs.writeFileSync(log, '1/1 Test: adds\nTest Passed.\nEnd testing: now\n');
  fs.utimesSync(log, now + 120, now + 120);
}

function change(t, edit) {
  const { repo, git } = repoWith(t);
  edit(repo);
  git('add', '-A');
  evidence(repo);
  return repo;
}

function ask(repo, command) {
  const input = JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command }, cwd: repo });
  const r = spawnSync(process.execPath, [HOOK], { input, encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  return r.stdout.trim() ? JSON.parse(r.stdout).hookSpecificOutput.permissionDecisionReason : null;
}

const editTest = (from, to) => (repo) => {
  const f = path.join(repo, 'tests', 'test_adds.cpp');
  fs.writeFileSync(f, fs.readFileSync(f, 'utf8').replace(from, to));
};

test('a commit with build and test evidence and no weakened test passes', (t) => {
  const repo = change(t, editTest('  }\n};', '  }\n  void more() { QVERIFY(1 < 2); }\n};'));
  assert.equal(ask(repo, 'git commit -m "test: 시험 추가"'), null);
});

test('changing an expected value without a reason line is blocked', (t) => {
  const repo = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);'));
  const reason = ask(repo, 'git commit -m "fix: 고침"');
  assert.match(reason ?? '', /시험 변경:/);
  assert.match(reason, /tests\/test_adds\.cpp/);
});

test('the reason line in the commit message lets it through', (t) => {
  const repo = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);'));
  assert.equal(ask(repo, 'git commit -m "fix: 고침" -m "시험 변경: 기댓값이 잘못돼 있었다"'), null);
});

test('the reason line inside a heredoc message counts', (t) => {
  const repo = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);'));
  const command = "git commit -q -F - <<'EOF'\nfix: 고침\n\n시험 변경: 기댓값이 잘못돼 있었다\nEOF";
  assert.equal(ask(repo, command), null);
});

test('skipping a test is blocked', (t) => {
  const repo = change(t, editTest('    QCOMPARE(1 + 1, 2);', '    QSKIP("later");\n    QCOMPARE(1 + 1, 2);'));
  assert.match(ask(repo, 'git commit -m "x"') ?? '', /시험 변경:/);
});

test('deleting a test file is blocked', (t) => {
  const repo = change(t, (r) => fs.rmSync(path.join(r, 'tests', 'test_adds.cpp')));
  assert.match(ask(repo, 'git commit -m "x"') ?? '', /시험 변경:/);
});

test('unregistering a test in CMakeLists.txt is blocked', (t) => {
  const repo = change(t, (r) => fs.writeFileSync(path.join(r, 'CMakeLists.txt'), 'project(x)\n'));
  assert.match(ask(repo, 'git commit -m "x"') ?? '', /시험 변경:/);
});

// Review findings 2026-10-03 (each probe passed through or was falsely blocked before the fix).
test('changing the expected value on the second line of an assertion is blocked', (t) => {
  const repo = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1,\n             2);'));
  assert.equal(ask(repo, 'git commit -m "style: 줄바꿈"'), null);
  const weak = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1,\n             3);'));
  assert.match(ask(weak, 'git commit -m "x"') ?? '', /시험 변경:/);
});

test('a renamed test file is compared with its old content', (t) => {
  const moved = (weaken) => (r) => {
    const text = fs.readFileSync(path.join(r, 'tests', 'test_adds.cpp'), 'utf8');
    fs.rmSync(path.join(r, 'tests', 'test_adds.cpp'));
    fs.writeFileSync(path.join(r, 'tests', 'test_sums.cpp'), weaken ? text.replace('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);') : text);
  };
  assert.equal(ask(change(t, moved(false)), 'git commit -m "refactor: 이름"'), null);
  assert.match(ask(change(t, moved(true)), 'git commit -m "x"') ?? '', /시험 변경:/);
});

test('dropping a name from a multi-line test filter list is blocked', (t) => {
  const repo = change(t, (r) => fs.writeFileSync(path.join(r, 'CMakeLists.txt'), CMAKE.replace('  more)', ')')));
  assert.match(ask(repo, 'git commit -m "x"') ?? '', /시험 변경:/);
});

test('a new test with a conditional skip is not weakening', (t) => {
  const repo = change(t, editTest('  }\n};', '  }\n  void optIn() {\n    if (qEnvironmentVariableIsEmpty("RUN"))\n      QSKIP("opt-in");\n    QVERIFY(true);\n  }\n};'));
  assert.equal(ask(repo, 'git commit -m "test: 선택 시험"'), null);
});

test('only a real reason line in the message unlocks', (t) => {
  const weak = () => change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);'));
  assert.match(ask(weak(), 'git commit -m "x" -m "시험 변경: 없음"') ?? '', /시험 변경:/);
  assert.match(ask(weak(), 'git log --grep "시험 변경:" && git commit -m "x"') ?? '', /시험 변경:/);
});

test('amending without a new message keeps the earlier reason line', (t) => {
  const repo = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);'));
  execFileSync('git', ['-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', 'fix', '-m', '시험 변경: 기댓값이 틀렸다'], { cwd: repo });
  fs.appendFileSync(path.join(repo, 'tests', 'test_adds.cpp'), '// note\n');
  execFileSync('git', ['add', '-A'], { cwd: repo });
  evidence(repo);
  assert.equal(ask(repo, 'git commit --amend --no-edit'), null);
});

test('a diff prefix setting in git config does not open the lock', (t) => {
  const repo = change(t, editTest('QCOMPARE(1 + 1, 2);', 'QCOMPARE(1 + 1, 3);'));
  execFileSync('git', ['config', 'diff.noprefix', 'true'], { cwd: repo });
  assert.match(ask(repo, 'git commit -m "x"') ?? '', /시험 변경:/);
});

// 2026-10-03: the four former baseline failures pass locally and in CI, so none is excused any more.
test('a failure in a former baseline test blocks the commit', (t) => {
  const repo = change(t, editTest('  }\n};', '  }\n  void more() { QVERIFY(1 < 2); }\n};'));
  const log = path.join(repo, 'build', 'Testing', 'Temporary', 'LastTest.log');
  fs.writeFileSync(log, '1/1 Test: workflow_engine\nTest Failed.\nEnd testing: now\n');
  const later = Date.now() / 1000 + 120;
  fs.utimesSync(log, later, later);
  fs.mkdirSync(path.join(repo, 'build', 'test-logs'), { recursive: true });
  fs.writeFileSync(path.join(repo, 'build', 'test-logs', 'workflow_engine.txt'),
    'FAIL!  : TestWorkflow::shapeEditing_livesInsideSelectTool() wrong\nTotals: 1 passed, 1 failed\n');
  assert.match(ask(repo, 'git commit -m "test: 시험 추가"') ?? '', /workflow_engine/);
});

test('moving or re-indenting an assertion unchanged is not weakening', (t) => {
  const repo = change(t, editTest('    QCOMPARE(1 + 1, 2);\n    QVERIFY(true);', '    QVERIFY(true);\n      QCOMPARE(1 + 1, 2);'));
  assert.equal(ask(repo, 'git commit -m "style: 순서"'), null);
});

// Line limit (docs/intent/2026-10-03-line-limit-precommit.md): the same rule as
// scripts/scorecard.ps1 -LineLimitOnly, checked before the commit instead of only on GitHub.
const writeSrc = (rel, text) => (repo) => fs.writeFileSync(path.join(repo, ...rel.split('/')), text);

test('growing a file past its recorded length is blocked', (t) => {
  const repo = change(t, writeSrc('src/big.cpp', linesOf(312)));
  const reason = ask(repo, 'git commit -m "fix: 고침"') ?? '';
  assert.match(reason, /src\/big\.cpp/);
  assert.match(reason, /312/);
});

test('a new C++ file over 300 lines is blocked, at 300 it passes', (t) => {
  assert.match(ask(change(t, writeSrc('src/new.cpp', linesOf(301))), 'git commit -m "feat: 새 파일"') ?? '', /src\/new\.cpp/);
  assert.equal(ask(change(t, writeSrc('src/new.cpp', linesOf(300, '\r\n'))), 'git commit -m "feat: 새 파일"'), null);
});

test('blank lines at the end count as on GitHub', (t) => {
  const reason = ask(change(t, writeSrc('src/new.cpp', `${linesOf(299)}\n\n`)), 'git commit -m "feat: 새 파일"') ?? '';
  assert.match(reason, /src\/new\.cpp: 301줄/);
});

test('editing a recorded file without growing it passes', (t) => {
  assert.equal(ask(change(t, writeSrc('src/big.cpp', linesOf(305))), 'git commit -m "refactor: 줄임"'), null);
});

test('raising a recorded length needs a reason line', (t) => {
  const raise = (repo) => {
    writeSrc('src/big.cpp', linesOf(312))(repo);
    writeSrc(BASELINE, '# C++ files already over the 300-line limit\n312 src/big.cpp\n')(repo);
  };
  assert.match(ask(change(t, raise), 'git commit -m "fix: 고침"') ?? '', /길이 기준 변경:/);
  assert.equal(ask(change(t, raise), 'git commit -m "fix: 고침" -m "길이 기준 변경: 다른 대화의 수정으로 늘었다"'), null);
  const onlyBaseline = (repo) => writeSrc(BASELINE, '# C++ files already over the 300-line limit\n320 src/big.cpp\n')(repo);
  assert.match(ask(change(t, onlyBaseline), 'git commit -m "docs: 기준"') ?? '', /길이 기준 변경:/);
});

test('adding a new baseline entry needs a reason line', (t) => {
  const add = writeSrc(BASELINE, '# C++ files already over the 300-line limit\n310 src/big.cpp\n400 src/later.cpp\n');
  assert.match(ask(change(t, add), 'git commit -m "docs: 기준"') ?? '', /src\/later\.cpp/);
  assert.equal(ask(change(t, add), 'git commit -m "docs: 기준" -m "길이 기준 변경: 곧 나눌 파일"'), null);
});

test('a header under tests/ over 300 lines is blocked', (t) => {
  assert.match(ask(change(t, writeSrc('tests/helper.h', linesOf(301))), 'git commit -m "test: 도우미"') ?? '', /tests\/helper\.h: 301줄/);
});

test('a lone CR ends a line as on GitHub', (t) => {
  assert.match(ask(change(t, writeSrc('src/new.cpp', linesOf(301, '\r'))), 'git commit -m "feat: 새 파일"') ?? '', /src\/new\.cpp: 301줄/);
});

test('-a counts the work tree, a plain commit counts what is staged', (t) => {
  const repo = change(t, writeSrc('src/big.cpp', linesOf(305)));
  writeSrc('src/big.cpp', linesOf(312))(repo); // grown again but not staged
  assert.match(ask(repo, 'git commit -a -m "fix: 고침"') ?? '', /src\/big\.cpp: 312줄/);
  assert.equal(ask(repo, 'git commit -m "fix: 고침"'), null);
});

test('lowering, dropping or deleting the baseline checks the listed file even outside the commit', (t) => {
  const lower = writeSrc(BASELINE, '# C++ files already over the 300-line limit\n305 src/big.cpp\n');
  assert.match(ask(change(t, lower), 'git commit -m "docs: 기준"') ?? '', /src\/big\.cpp: 310줄/);
  const drop = writeSrc(BASELINE, '# C++ files already over the 300-line limit\n');
  assert.match(ask(change(t, drop), 'git commit -m "docs: 기준"') ?? '', /src\/big\.cpp: 310줄/);
  const remove = (repo) => fs.rmSync(path.join(repo, ...BASELINE.split('/')));
  assert.match(ask(change(t, remove), 'git commit -m "docs: 기준"') ?? '', /src\/big\.cpp: 310줄/);
  const shrinkBoth = (repo) => {
    writeSrc('src/big.cpp', linesOf(305))(repo);
    lower(repo);
  };
  assert.equal(ask(change(t, shrinkBoth), 'git commit -m "refactor: 줄임"'), null);
});

// 2026-10-23: development runs on Linux, where the evidence lives in build-linux/ (no Release/ and no .exe).
function linuxEvidence(repo, { exeOffset = 60, logOffset = 120, log = '1/1 Test: adds\nTest Passed.\nEnd testing: now\n' } = {}) {
  const dir = path.join(repo, 'build-linux');
  fs.mkdirSync(path.join(dir, 'Testing', 'Temporary'), { recursive: true });
  fs.writeFileSync(path.join(dir, 'CMakeCache.txt'), '');
  const now = Date.now() / 1000;
  for (const exe of ['ka-hgis', 'ka_adds_tests']) {
    fs.writeFileSync(path.join(dir, exe), '');
    fs.utimesSync(path.join(dir, exe), now + exeOffset, now + exeOffset);
  }
  const lastLog = path.join(dir, 'Testing', 'Temporary', 'LastTest.log');
  fs.writeFileSync(lastLog, log);
  fs.utimesSync(lastLog, now + logOffset, now + logOffset);
}

function linuxChange(t, edit, options) {
  const { repo, git } = repoWith(t);
  edit(repo);
  git('add', '-A');
  linuxEvidence(repo, options);
  return repo;
}

test('a Linux build in build-linux/ with a later passing ctest lets a src change through', (t) => {
  assert.equal(ask(linuxChange(t, writeSrc('src/small.cpp', linesOf(10))), 'git commit -m "feat: 작은 파일"'), null);
});

test('a Linux executable older than the changed file blocks the commit', (t) => {
  const reason = ask(linuxChange(t, writeSrc('src/small.cpp', linesOf(10)), { exeOffset: -600, logOffset: 120 }), 'git commit -m "feat: 작은 파일"');
  assert.match(reason ?? '', /build-linux\/ka-hgis/);
});

test('a failed ctest in build-linux/ blocks the commit', (t) => {
  const reason = ask(linuxChange(t, writeSrc('src/small.cpp', linesOf(10)), { log: '1/1 Test: adds\nTest Failed.\nEnd testing: now\n' }), 'git commit -m "feat: 작은 파일"');
  assert.match(reason ?? '', /adds/);
});

test('without any build directory the Linux first-build command is named too', (t) => {
  const { repo, git } = repoWith(t);
  writeSrc('src/small.cpp', linesOf(10))(repo);
  git('add', '-A');
  assert.match(ask(repo, 'git commit -m "feat: 작은 파일"') ?? '', /cmake --preset linux/);
});
