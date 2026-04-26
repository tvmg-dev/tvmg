#!/bin/bash

mkdir -p /root/jekyll

cd /root/jekyll

echo 'source "https://rubygems.org"' > Gemfile && \
    echo 'gem "jekyll"' >> Gemfile && \
    echo 'gem "kramdown-parser-gfm"' >> Gemfile && \
    echo 'gem "webrick"' >> Gemfile

bundle install
chmod -R 755 /usr/local/bundle

rm -rf /root/jekyll
