FROM ruby:3.2-slim

# apt-get -y option prevents apt from confirming installation & 
# DEBIAN_FRONTEND prevents timezone dependency from querying location

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
   apt-get -y install apt-utils \
   build-essential git nodejs curl ca-certificates

# Install jekyll

RUN gem install --no-document bundler jekyll

# Copy and run install

COPY ./install-bundle.sh /usr/local/bin/
RUN /usr/local/bin/install-bundle.sh

# Clean up

RUN apt-get clean && rm -rf /var/lib/apt/lists/* && rm -rf /var/cache/apt/archives/*

# Exposing on port 4000

EXPOSE 4000

RUN mkdir -p /working && chmod 777 /working
WORKDIR /working

ENTRYPOINT ["jekyll", "serve", "--host", "0.0.0.0"]