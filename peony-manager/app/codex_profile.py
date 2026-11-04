"""Safe, small editor for Peony's Codex CLI profile preferences.

The Codex CLI owns this file.  Peony only edits the two simple root string
settings it exposes in its UI; everything else is kept byte-for-byte where
possible.  This intentionally is not a general TOML parser.
"""
import fcntl
import os
import re
import stat
import tempfile
from contextlib import contextmanager
from pathlib import Path


PROFILE_NAME = "peony-cli.config.toml"
MODEL_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:/+-]{0,127}$")
REASONING_EFFORTS = ("none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra")

_TABLE_HEADER = re.compile(
    r"^\s*(?:\[\s*[^\[\]\r\n]+\s*\]|\[\[\s*[^\[\]\r\n]+\s*\]\])"
    r"\s*(?:#.*)?(?:\r?\n)?$"
)
_SETTING_PREFIX = re.compile(
    r"^\s*(?P<key>model|model_reasoning_effort|\"model\"|'model'|"
    r"\"model_reasoning_effort\"|'model_reasoning_effort')\s*="
)
_DOTTED_SETTING_PREFIX = re.compile(r"^\s*(?:model|model_reasoning_effort)\s*\.")
_SAFE_SETTING = re.compile(
    r"^(?P<prefix>\s*(?P<key>model|model_reasoning_effort)\s*=\s*)"
    r"(?P<quote>[\"'])(?P<value>[^\\\"'\r\n]*)(?P=quote)"
    r"(?P<suffix>\s*(?:#.*)?(?:\r?\n)?)$"
)


def profile_path():
    """Return the Codex profile path without changing the process environment."""
    codex_home = os.environ.get("CODEX_HOME")
    if codex_home:
        root = Path(codex_home)
        if not root.is_absolute():
            raise ValueError("CODEX_HOME 必须是绝对路径，无法安全定位 Codex profile")
    else:
        root = Path.home() / ".codex"
    return root / PROFILE_NAME


def _lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    lock_name = str(path) + ".peony.lock"
    fd = os.open(lock_name, os.O_CREAT | os.O_RDWR, 0o600)
    os.fchmod(fd, 0o600)
    return fd


@contextmanager
def _profile_lock(path):
    fd = _lock(path)
    with os.fdopen(fd, "r+") as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def _validate_value(key, value):
    if not isinstance(value, str) or "\0" in value or "\n" in value or "\r" in value:
        raise ValueError("Codex profile %s 必须是单行字符串" % key)
    if value == "":
        return value
    if key == "model":
        if not MODEL_ID.fullmatch(value):
            raise ValueError("Codex profile model 必须是有效的模型标识")
    elif value not in REASONING_EFFORTS:
        raise ValueError("Codex profile model_reasoning_effort 必须是已知推理强度")
    return value


def _container_balance(value):
    """Count TOML array/inline-table delimiters outside strings/comments."""
    square = 0
    curly = 0
    quote = None
    escaped = False
    for char in value:
        if quote:
            if quote == '"' and escaped:
                escaped = False
            elif quote == '"' and char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char == "#":
            break
        if char in ("'", '"'):
            quote = char
        elif char == "[":
            square += 1
        elif char == "]":
            square -= 1
        elif char == "{":
            curly += 1
        elif char == "}":
            curly -= 1
        if square < 0 or curly < 0:
            raise ValueError("无法安全编辑 Codex profile：根配置含不匹配的容器")
    if quote:
        raise ValueError("无法安全编辑 Codex profile：根配置含未闭合字符串")
    return square, curly


def _root_settings(lines):
    """Read only unquoted root keys, rejecting syntax this editor cannot prove safe."""
    values = {"model": "", "model_reasoning_effort": ""}
    indexes = {"model": [], "model_reasoning_effort": []}
    root_end = len(lines)
    for index, line in enumerate(lines):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if _TABLE_HEADER.match(line):
            root_end = index
            break
        # A malformed header or a multiline root value could make apparent
        # assignments below it belong to a value instead of the TOML root.
        if line.lstrip().startswith("["):
            raise ValueError("无法安全编辑 Codex profile：根配置含无法识别的表或多行数组")
        if line.lstrip().startswith(("\"", "'")):
            raise ValueError("无法安全编辑 Codex profile：根配置含引号键")
        if _DOTTED_SETTING_PREFIX.match(line):
            raise ValueError("无法安全编辑 Codex profile：根配置含模型项的 dotted key")
        if _SETTING_PREFIX.match(line):
            match = _SAFE_SETTING.match(line)
            if not match:
                raise ValueError("无法安全编辑 Codex profile：目标项不是简单的单行字符串")
            key = match.group("key")
            value = _validate_value(key, match.group("value"))
            indexes[key].append(index)
            values[key] = value
            continue
        value_text = line
        if "'''" in value_text or '"""' in value_text:
            raise ValueError("无法安全编辑 Codex profile：根配置含多行字符串")
        # Do not let a multiline array/table hide text that looks like one of
        # the settings we own.  Inline containers are fine; open containers
        # at the end of a root assignment are not.
        if re.match(r"^\s*[^#\s].*?=", line):
            rhs = line.split("=", 1)[1]
            square, curly = _container_balance(rhs)
            # An open container could make later lines appear to be root keys.
            if square or curly:
                raise ValueError("无法安全编辑 Codex profile：根配置含多行数组或表")
    for key, occurrences in indexes.items():
        if len(occurrences) > 1:
            raise ValueError("无法安全编辑 Codex profile：根配置重复定义 %s" % key)
    return values, indexes, root_end


def _read_text(path):
    if path.is_symlink():
        raise ValueError("无法安全编辑 Codex profile：配置文件是符号链接")
    raw = path.read_bytes()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("无法安全编辑 Codex profile：配置文件不是 UTF-8")
    lines = text.splitlines(True)
    values, indexes, root_end = _root_settings(lines)
    return raw, lines, values, indexes, root_end


def read_preferences():
    """Read the Peony profile's root model and effort; never creates a file."""
    path = profile_path()
    if not path.exists():
        return {"model": "", "reasoningEffort": ""}
    _, _, values, _, _ = _read_text(path)
    return {"model": values["model"], "reasoningEffort": values["model_reasoning_effort"]}


def _inline_comment(line, match):
    suffix = match.group("suffix")
    comment = re.search(r"#.*", suffix)
    if not comment:
        return None
    newline = "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
    indent = re.match(r"^\s*", line).group(0)
    return indent + comment.group(0) + newline


def _render(lines, values, indexes, root_end, changes):
    replacements = {}
    removals = set()
    comments = {}
    key_map = {"model": "model", "reasoningEffort": "model_reasoning_effort"}
    for field, value in changes.items():
        key = key_map[field]
        occurrences = indexes[key]
        if not value:
            if occurrences:
                index = occurrences[0]
                match = _SAFE_SETTING.match(lines[index])
                removals.add(index)
                comment = _inline_comment(lines[index], match)
                if comment:
                    comments[index] = comment
            continue
        if occurrences:
            index = occurrences[0]
            match = _SAFE_SETTING.match(lines[index])
            replacements[index] = (match.group("prefix") + match.group("quote") +
                                   value + match.group("quote") + match.group("suffix"))

    output = []
    for index, line in enumerate(lines):
        if index in comments:
            output.append(comments[index])
        if index in removals:
            continue
        output.append(replacements.get(index, line))

    added = []
    for field, value in changes.items():
        key = key_map[field]
        if value and not indexes[key]:
            added.append('%s = "%s"' % (key, value))
    if added:
        newline = "\r\n" if any(line.endswith("\r\n") for line in lines) else "\n"
        # Table contents stay untouched: new root keys go immediately before
        # the first table (or at EOF for a table-free profile).
        insert_at = root_end - sum(1 for index in removals
                                   if index < root_end and index not in comments)
        for offset, setting in enumerate(added):
            output.insert(insert_at + offset, setting + newline)
        if insert_at == len(output) - len(added) and insert_at > 0:
            previous = output[insert_at - 1]
            if not previous.endswith(("\n", "\r")):
                output[insert_at - 1] = previous + newline
    return "".join(output)


def _atomic_write(path, content, original, existed):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ValueError("无法安全编辑 Codex profile：配置文件是符号链接")
    if existed:
        try:
            if path.read_bytes() != original:
                raise ValueError("Codex profile 在保存期间已被修改，请重试")
            mode = stat.S_IMODE(path.stat().st_mode)
        except OSError:
            raise ValueError("Codex profile 在保存期间已变化，请重试")
    else:
        mode = 0o600
        if path.exists():
            raise ValueError("Codex profile 在保存期间已创建，请重试")

    fd, temporary = tempfile.mkstemp(prefix=".peony-cli-", suffix=".toml", dir=str(path.parent))
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, "wb") as stream:
            stream.write(content.encode("utf-8"))
            stream.flush()
            os.fsync(stream.fileno())
        if existed:
            if path.is_symlink() or path.read_bytes() != original:
                raise ValueError("Codex profile 在保存期间已被修改，请重试")
            os.replace(temporary, str(path))
        else:
            # A native launcher may create the profile at the same time.
            # Linking publishes only if the destination is still absent.
            os.link(temporary, str(path))
            os.unlink(temporary)
        directory_fd = os.open(str(path.parent), os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def update_preferences(changes, seed=None):
    """Atomically update only explicit model/effort fields.

    Empty strings remove their root key.  ``seed`` is consulted only when a
    profile does not yet exist, allowing migration from Peony's old JSON file.
    """
    if not isinstance(changes, dict) or any(key not in ("model", "reasoningEffort") for key in changes):
        raise ValueError("只能更新 Codex profile 的模型和推理强度")
    clean = {}
    for key, value in changes.items():
        actual_key = "model" if key == "model" else "model_reasoning_effort"
        clean[key] = _validate_value(actual_key, value)
    seed_values = {"model": "", "reasoningEffort": ""}
    if seed is not None:
        if not isinstance(seed, dict):
            raise ValueError("profile 迁移种子必须是对象")
        for key in seed_values:
            if key in seed:
                actual_key = "model" if key == "model" else "model_reasoning_effort"
                seed_values[key] = _validate_value(actual_key, seed[key])
    if not clean:
        return read_preferences()

    path = profile_path()
    if not path.exists():
        initial_values = {field: clean.get(field, seed_values[field])
                          for field in seed_values}
        if not any(initial_values.values()):
            # An explicit "follow Codex" choice with no profile to clear does
            # not create CODEX_HOME or a sidecar lock file.
            return {"model": "", "reasoningEffort": ""}
    with _profile_lock(path):
        existed = path.exists()
        if existed:
            original, lines, values, indexes, root_end = _read_text(path)
        else:
            original = b""
            lines = []
            values = {"model": "", "model_reasoning_effort": ""}
            indexes = {"model": [], "model_reasoning_effort": []}
            root_end = 0
            # Existing Peony preferences provide the one-time migration seed;
            # explicit user input wins over the seed for that setting.
            for field, key in (("model", "model"), ("reasoningEffort", "model_reasoning_effort")):
                value = clean.get(field, seed_values[field])
                if value:
                    values[key] = value
                    clean.setdefault(field, value)
        output = _render(lines, values, indexes, root_end, clean)
        if output.encode("utf-8") != original:
            _atomic_write(path, output, original, existed)
    return read_preferences()
