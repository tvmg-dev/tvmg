#!/bin/bash

# The repo owner deleted the tags before 0.4.0 sometime in 2026
# so need to get the commit for 0.3.6 and create the archive for
# the docker build.
#
# The git repo. has been committed with DOS line-endings so we
# have to convert them to LF only for patch application.

git clone https://github.com/mobizt/ReadyMail.git 
git -C ReadyMail archive --format=tar.gz --prefix=ReadyMail-0.3.6/ -o ../ReadyMail-0.3.6-dos.tar.gz 38d172a

tar -xf ReadyMail-0.3.6-dos.tar.gz
find ReadyMail-0.3.6 -type f \( -name "*.h" -o -name "*.cpp" \) -exec sed -i 's/\r$//' {} +
tar -czf ReadyMail-0.3.6.tar.gz ReadyMail-0.3.6

rm -rf ReadyMail
rm -rf ReadyMail-0.3.6
rm -f ReadyMail-0.3.6-dos.tar.gz
