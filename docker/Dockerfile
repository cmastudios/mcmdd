FROM		ubuntu:14.04
MAINTAINER	Connor Monahan admin@cmastudios.me"

RUN		echo "deb http://archive.ubuntu.com/ubuntu utopic main universe" > /etc/apt/sources.list
RUN		echo "deb http://archive.ubuntu.com/ubuntu utopic-security main universe" >> /etc/apt/sources.list
RUN		apt-get update
RUN		apt-get install -y wget
RUN		wget http://download.opensuse.org/repositories/home:cmastudios/xUbuntu_14.04/Release.key
RUN		apt-key add - < Release.key 
RUN		echo 'deb http://download.opensuse.org/repositories/home:/cmastudios/xUbuntu_14.04/ /' >> /etc/apt/sources.list.d/mcmdd.list
RUN		apt-get update
RUN		apt-get install -y mcmdd
RUN		apt-get install -y openjdk-7-jre-headless
RUN		apt-get install -y openjdk-8-jre-headless

VOLUME		/var/lib/mcmdd

EXPOSE		8361 25565
CMD		["mcmdd","-d","/var/lib/mcmdd","-n"]
