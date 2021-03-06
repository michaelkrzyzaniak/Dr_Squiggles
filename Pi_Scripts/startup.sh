#! /bin/sh

cd /home/pi/Dr_Squiggles/
git pull
mkdir /home/pi/Dr_Squiggles/Beat-and-Tempo-Tracking/
cd /home/pi/Dr_Squiggles/Beat-and-Tempo-Tracking/
git pull
sudo chmod a+x /home/pi/Dr_Squiggles/Pi_Scripts/startup.sh
cd /home/pi/Dr_Squiggles/Main
sudo apt-get install libasound2-dev
gcc sq.c core/*.c ../Robot_Communication_Framework/*.c ../Beat-and-Tempo-Tracking/src/*.c Rhythm_Generators/*.c extras/*.c -lasound -lm -lpthread -lrt -O2 -o sq
sudo cp ./sq /usr/local/bin/sq
gcc op.c core/*.c ../Robot_Communication_Framework/*.c ../Beat-and-Tempo-Tracking/src/*.c Rhythm_Generators/*.c extras/*.c -lasound -lm -lpthread -lrt -O2 -o op
sudo cp ./op /usr/local/bin/op
#/home/pi/Dr_Squiggles/Main/a.out
