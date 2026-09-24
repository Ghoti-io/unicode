# ICU, the second opinion on segmentation - and the only one there is.
#
# design.md section 12.1 names this oracle and section 12.4 the shape: the
# driver links **only ICU**, as `regex`'s `pcre2_match` links only pcre2, so
# that the reference cannot reach the implementation it answers for. This
# library never enters this image; there is no tree to build in here and
# nothing to keep isolated by convention.
#
# **Built here rather than installed, because of the version.** ICU carries a
# Unicode version of its own and that is the pin that decides what a
# disagreement means (containers/IMAGES). Debian 13 gives ICU 76.1, which is
# Unicode 16.0 against this library's 17.0.0 - one release short, so every
# tailoring difference would arrive mixed in with a release of Consortium
# changes and every run would need triage. ICU 78 is Unicode 17.0 and is
# *released*, so unlike the CPython oracle there is no reason to gate on a
# reference that does not match: one pin, and it is the matching one.
#
# ICU 78.3 is in Debian sid as 78.3-2 and installing it from there was the
# cheap option. It is the wrong option for this particular gate. sid moves
# continuously, so an exact apt pin into it expires sooner than a stable one
# would - and CONTAINERS.md section 2.6 already reasons that out for vim: a
# version-pinned apt install eventually fails to resolve, while a source
# release has no expiry. This is the one gate here whose disagreement means a
# defect, so an expiring pin is the worst place to put it.
#
# What is pinned, per section 2.6's two-part rule for a built-here image:
#
#   - the base, by digest
#   - the source tarball, by the **SHA-512 the Unicode Consortium publishes**
#     for it in SHASUM512.txt beside the release. Not the git tag: a tag is a
#     name and this is the bytes. A tarball regenerated with a different
#     mtime would fail here, which is the correct outcome for a reference.
#   - and the run-time version check in oracle_env.py, which is the real
#     guarantee, because two builds of one Dockerfile are not two copies of
#     one image.

FROM docker.io/library/debian@sha256:7792b1f7702a86946cd518db72b6a407302c3e9bc1635634368b878189e8221c

ARG ICU_VERSION=78.3
ARG ICU_TARBALL=icu4c-78.3-sources.tgz
ARG ICU_SHA512=04a49455e1489030c520a4bfd2664fa2171e7938d08f2acdbbcb1fda976639fd8b1f0704f2eec89ba59a7b6d118ceaab6ec5a096e40d9085a0895d91ce225245

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl g++ make pkg-config libc6-dev python3 \
 && rm -rf /var/lib/apt/lists/*

RUN cd /tmp \
 && curl -fsSLO "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION}/${ICU_TARBALL}" \
 && printf '%s  %s\n' "${ICU_SHA512}" "${ICU_TARBALL}" | sha512sum -c - \
 && tar xzf "${ICU_TARBALL}" \
 && cd icu/source \
 && ./runConfigureICU Linux \
      --prefix=/usr/local \
      --disable-samples --disable-tests --disable-extras --disable-layoutex \
 && make -j"$(nproc)" \
 && make install \
 && ldconfig \
 && cd /tmp && rm -rf /tmp/icu /tmp/"${ICU_TARBALL}"

# A version probe compiled into the image, so that oracle_env.py can ask what
# this reference is without first compiling the driver.
#
# It reports U_UNICODE_VERSION from the headers the driver itself will be
# compiled against, which is the right source for the claim: `icuinfo` prints
# the same number wrapped in XML, and parsing that would put a sed script
# between the pin check and the fact it is checking.
RUN printf '%s\n' \
      '#include <unicode/uchar.h>' \
      '#include <unicode/uversion.h>' \
      '#include <stdio.h>' \
      'int main(void) {' \
      '  printf("ICU %s, Unicode %s", U_ICU_VERSION, U_UNICODE_VERSION);' \
      '  puts("");' \
      '  return 0;' \
      '}' \
    > /tmp/icu-version.c \
 && gcc -O1 -o /usr/local/bin/icu-version /tmp/icu-version.c -licuuc \
 && rm /tmp/icu-version.c \
 && /usr/local/bin/icu-version

# So that the driver, compiled at run time against the mounted source, finds
# both the headers and the libraries without the caller spelling paths.
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8
ENV LD_LIBRARY_PATH=/usr/local/lib

ENTRYPOINT []
CMD ["icu-version"]
