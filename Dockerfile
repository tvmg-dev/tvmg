FROM ruby:3.2-slim

# apt-get -y option prevents apt from confirming installation & 
# DEBIAN_FRONTEND prevents timezone dependency from querying location

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
   apt-get -y install apt-utils openssh-client iputils-ping net-tools sudo nano \
   build-essential git nodejs curl ca-certificates

# Install jekyll

RUN gem install --no-document bundler jekyll

# Standard version info, setup-user & ssh manager script, and a bashrc snippet

COPY ./imageversion ./setup-user.sh ./manage-ssh.sh ./bashrc.snippet /usr/local/bin/

# Copy and run install if required

COPY ./custom-image-install.sh /usr/local/bin/
RUN /usr/local/bin/custom-image-install.sh

# Clean up

RUN apt-get clean && rm -rf /var/lib/apt/lists/* && rm -rf /var/cache/apt/archives/*

# Now label as interactive

LABEL csInteractive="y"

# We want SSH & SUDO, not GIT

ENV ENABLE_SSH=y
ENV ENABLE_SUDO=y
ENV ENABLE_GIT=y

EXPOSE 4000

# Default command - just have bash here

CMD ["/bin/bash"]
