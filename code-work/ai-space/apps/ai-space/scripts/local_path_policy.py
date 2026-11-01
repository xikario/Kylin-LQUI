"""Canonical local-file boundaries shared by inventory and extraction."""
import os


def canonical(path):
    return os.path.realpath(os.path.abspath(os.path.expanduser(str(path))))


def inside(path, folder):
    child, parent = canonical(path), canonical(folder)
    return child == parent or child.startswith(parent.rstrip(os.sep) + os.sep)


def excluded(path, folders):
    return any(inside(path, folder) for folder in folders)


def local_read_allowed(path, roots, excludes):
    return os.path.isabs(path) and not excluded(path, excludes) and any(inside(path, root) for root in roots)
