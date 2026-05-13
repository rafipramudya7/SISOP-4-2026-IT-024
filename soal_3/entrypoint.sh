#!/bin/bash

set -e

mkdir -p /libraryit/ebooks
mkdir -p /libraryit/papers
mkdir -p /libraryit/sourcecode
mkdir -p /libraryit/docs
mkdir -p /logs

touch /logs/libraryit.log

groupadd -f readonly
groupadd -f staff

id member >/dev/null 2>&1 || useradd -m member
id contributor >/dev/null 2>&1 || useradd -m contributor
id librarian >/dev/null 2>&1 || useradd -m librarian

echo "member:member123" | chpasswd
echo "contributor:contrib456" | chpasswd
echo "librarian:lib789" | chpasswd

(echo member123; echo member123) | smbpasswd -s -a member
(echo contrib456; echo contrib456) | smbpasswd -s -a contributor
(echo lib789; echo lib789) | smbpasswd -s -a librarian

usermod -aG readonly member
usermod -aG staff contributor
usermod -aG staff librarian

chmod -R 770 /libraryit/ebooks
chmod -R 770 /libraryit/papers
chmod -R 750 /libraryit/sourcecode
chmod -R 755 /libraryit/docs

exec smbd --foreground --no-process-group