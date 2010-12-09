set -e

cat >/etc/apt/sources.list.d/devstructure.list <<EOF
deb http://packages.devstructure.com maverick main
EOF
wget -O - http://packages.devstructure.com/keyring.gpg | sudo apt-key add -
apt-get update
apt-get -y install build-essential debra libfuse-dev libglib2.0-dev m4
gem install ronn # Assumes Ruby and RubyGems are here, as in Vagrant.
