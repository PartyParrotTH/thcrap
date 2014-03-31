#!/usr/bin/env python3

# Touhou Community Reliant Automatic Patcher
# Scripts
#
# ----
#
"""Builds and updates a patch repository for the
Touhou Community Reliant Automatic Patcher."""

import shutil
import os
import argparse
import zlib
import utils

parser = argparse.ArgumentParser(
    description=__doc__
)
parser.add_argument(
    '-f', '--from',
    metavar='path',
    help='Repository source path. Also receives copies of the files.js, '
         'patch.js and repo.js files modified by this script.',
    default='.',
    type=str,
    dest='f'
)

parser.add_argument(
    '-t', '--to',
    help='Destination directory. If different from the source directory, '
         'all files of all patches are copied there.',
    metavar='path',
    default='.',
    type=str,
    dest='t'
)


def str_slash_normalize(string):
	return string.replace('\\', '/')


def enter_missing(obj, key, prompt):
    while not key in obj or not obj[key].strip():
        obj[key] = input(prompt)


def sizeof_fmt(num):
    for x in ['bytes', 'KB', 'MB', 'GB']:
        if num < 1024.0:
            return "%3.1f %s" % (num, x)
        num /= 1024.0
    return "%3.1f %s" % (num, 'TB')


def patch_build(patch_id, servers, f, t):
    """Updates the patch in the [f]/[patch_id] directory.

    Ensures that patch.js contains all necessary keys and values, then updates
    the checksums in files.js and, if [t] differs from [f], copies all patch
    files from [f] to [t].

    Returns the contents of the patch ID key in repo.js."""
    f_path, t_path = [os.path.join(i, patch_id) for i in [f, t]]

    # Prepare patch.js.
    f_patch_fn = os.path.join(f_path, 'patch.js')
    patch_js = utils.json_load(f_patch_fn)

    enter_missing(
        patch_js, 'title', 'Enter a nice title for "{}": '.format(patch_id)
    )
    patch_js['id'] = patch_id
    patch_js['servers'] = []
    # Delete obsolete keys.
    if 'files' in patch_js:
        del(patch_js['files'])

    for i in servers:
        url = os.path.join(i, patch_id) + '/'
        patch_js['servers'].append(str_slash_normalize(url))
    utils.json_store(f_patch_fn, patch_js)

    files_js = {}
    patch_size = 0
    print(patch_id, end='')
    for root, dirs, files in os.walk(f_path):
        for fn in utils.patch_files_filter(files):
            print('.', end='')
            f_fn = os.path.join(root, fn)
            patch_fn = f_fn[len(f_path) + 1:]
            t_fn = os.path.join(t_path, patch_fn)

            with open(f_fn, 'rb') as f_file:
                f_file_data = f_file.read()
                f_sum = zlib.crc32(f_file_data) & 0xffffffff

            files_js[str_slash_normalize(patch_fn)] = f_sum
            patch_size += len(f_file_data)
            del(f_file_data)
            os.makedirs(os.path.dirname(t_fn), exist_ok=True)
            if f != t:
                shutil.copy2(f_fn, t_fn)

    utils.json_store('files.js', files_js, dirs=[f_path, t_path])
    print(
        '{num} files, {size}'.format(
            num=len(files_js), size=sizeof_fmt(patch_size)
        )
    )
    return patch_js['title']


def repo_build(f, t):
    try:
        f_repo_fn = os.path.join(f, 'repo.js')
        repo_js = utils.json_load(f_repo_fn)
    except FileNotFoundError:
        print(
            'No repo.js found in the source directory. '
            'Creating a new repository.'
        )
        repo_js = {}

    enter_missing(repo_js, 'id', 'Enter a repository ID: ')
    enter_missing(repo_js, 'title', 'Enter a nice repository title: ')
    enter_missing(repo_js, 'contact', 'Enter a contact e-mail address: ')
    utils.json_store('repo.js', repo_js, dirs=[f, t])

    while not 'servers' in repo_js or not repo_js['servers'][0].strip():
        repo_js['servers'] = [input(
            'Enter the public URL of your repository '
            '(the path that contains repo.js): '
        )]
    repo_js['patches'] = {}
    for root, dirs, files in os.walk(f):
        del(dirs)
        if 'patch.js' in files:
            patch_id = os.path.basename(root)
            repo_js['patches'][patch_id] = patch_build(
                patch_id, repo_js['servers'], f, t
            )
    print('Done.')
    utils.json_store('repo.js', repo_js, dirs=[f, t])


if __name__ == '__main__':
    arg = parser.parse_args()
    repo_build(arg.f, arg.t)