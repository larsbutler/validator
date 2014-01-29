import subprocess as sp
import sys

from datetime import datetime as dt
from debian import changelog


#: This changelog template produces a changelog entry,
#: which looks like the following:
#:
#: zvm-validator (0.9-20140129134254-gitfdf17c3) precise; urgency=low
#:
#:   * Add packaging scripts
#:
#: -- Lars Butler (larsbutler) <lars.butler@gmail.com>  Wed, 29 Jan 2014 13:42:54 +0000
#:
CHANGELOG_TEMPLATE = """\
%(pkg_name)s (%(version)s-%(dt_int)s-git%(short_hash)s-%(release)s) precise; urgency=low

  * %(merge_msg)s

 -- %(debfullname)s <%(debemail)s>  %(full_datetime)s

"""
DEFAULT_RELEASE = 1


def latest_package(fullname, email, pkg_name, version, ppa=None):
    """
    Generate and prepend an entry to the debian/changelog, the create a
    "latest" package. Changelog entry details are generated from the latest
    commit in git and the current date/time.
    """
    short_hash, merge_msg = _get_hash_merge_msg()
    now = dt.utcnow()
    _update_changelog(short_hash, merge_msg, fullname, email, pkg_name,
                      version, now)
    src_pkg_file, bin_pkg_file = _create_packages(
        pkg_name, version, short_hash, now
    )
    if ppa is not None:
        _publish_package(src_pkg_file, ppa)


def _sp_run(cmd):
    return sp.check_call(cmd.split())


def _create_packages(pkg_name, version, short_hash, now):
    tar_name = '%(pkg)s_%(version)s-%(dt_int)s-git%(short_hash)s.orig.tar.gz'
    tar_name %= dict(pkg=pkg_name,
                     version=version,
                     dt_int=now.strftime('%Y%m%d%H%M%S'),
                     short_hash=short_hash)
    pkg_file = ('%(pkg)s_%(version)s-%(dt_int)s-git%(short_hash)s-%(rel)s'
                '%%(suffix)s')
    pkg_file %= dict(
        pkg=pkg_name,
        version=version,
        dt_int=now.strftime('%Y%m%d%H%M%S'),
        short_hash=short_hash,
        rel=DEFAULT_RELEASE,
    )
    src_pkg_file = pkg_file % dict(suffix='_source.changes')
    bin_pkg_file = pkg_file % dict(suffix='_amd64.deb')

    archive = ('tar czf ../%(tar)s Makefile native_client/ README test/ '
               'packager.py')
    archive %= dict(tar=tar_name)
    _sp_run(archive)

    # build a binary package
    _sp_run('debuild')
    # build a source package
    _sp_run('debuild -S')

    return src_pkg_file, bin_pkg_file


def _publish_package(src_package_file, ppa):
    publish = 'dput %(ppa)s ../%(src_pkg)s'
    publish %= dict(src_pkg=src_package_file, ppa=ppa)
    _sp_run(publish)


def _get_hash_merge_msg():
    """
    Get the the short hash and merge message from the latest git commit, which
    is assumed to be a merge commit.
    """
    cmd = 'git log -n 1 --abbrev-commit'
    output = sp.check_output(cmd.split()).strip().split('\n')
    short_hash = output[0].split()[1]
    merge_msg = output[-1].strip()

    return short_hash, merge_msg


def _update_changelog(short_hash, merge_msg, fullname, email, pkg_name,
                      version, now):
    """
    Re-write the changelog, prepending the new generated entry.
    """
    changelog_entry = CHANGELOG_TEMPLATE % dict(
        pkg_name=pkg_name,
        version=version,
        # year month day hour minute second,
        # which makes this revision number easy to sort by time
        dt_int=now.strftime('%Y%m%d%H%M%S'),
        short_hash=short_hash,
        merge_msg=merge_msg,
        debfullname=fullname,
        debemail=email,
        full_datetime=now.strftime('%a, %d %b %Y %H:%M:%S +0000'),
        release=DEFAULT_RELEASE,
    )

    changelog_file = 'debian/changelog'
    with open(changelog_file) as fh:
        cl_content = fh.read()
    with open(changelog_file, 'w') as fh:
        fh.write(changelog_entry)
        fh.write(cl_content)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print('Usage: python %s NAME EMAIL [PPA]\n'
              'PPA (optional) should include the `ppa` prefix. Example: '
              'ppa:zerovm-ci/zervovm-latest' % __file__)
        sys.exit(1)

    fullname = sys.argv[1]
    email = sys.argv[2]

    # PPA publishing is optional
    ppa = None
    if len(sys.argv) == 3:
        ppa = sys.argv[3]

    # parse the upstream version and package name from the changelog
    with open('debian/changelog') as cl_fp:
        cl = changelog.Changelog(cl_fp)
        pkg_name = cl.get_package()
        version = cl.version.upstream_version
    latest_package(fullname, email, pkg_name, version, ppa=ppa)
