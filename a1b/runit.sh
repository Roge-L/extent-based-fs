#!/bin/bash

#save cur directory into a variable, we need it later
cwd=$(pwd)
#make directory if doesn't exist
mkdir -p /tmp/lamroger

# partition a disk for our usage
truncate -s 65536 disk

# mount our file system, with extra parameters just in case we have run this once already
./mkfs.a1fs -z -f -i 16 disk

# just in case something is already there...
fusermount -u /tmp/lamroger

# launch our file system
./a1fs disk /tmp/lamroger

# change directory to vfs location
cd /tmp/lamroger

#show data at initialization
stat -f .
ls -la

#create a directory, and show data
mkdir /tmp/lamroger/newdir
stat -f . 
ls -la

#create a second idrectory, and show data
mkdir /tmp/lamroger/tempdir
stat -f . 
ls -la

#remove our second directory, and show data
rmdir /tmp/lamroger/tempdir
stat -f . 
ls -la

#create a new file
touch /tmp/lamroger/tempfile
stat -f . 
ls -la

#remove temp file we just made
rm /tmp/lamroger/tempfile
stat -f . 
ls -la

#enter created directory newdir, and create a nested directory
cd /tmp/lamroger/newdir

#create a file inside the first directory, newdir
touch /tmp/lamroger/newdir/newfile
stat -f .
ls -la

#write hello world into newfile
echo 'hello world' > /tmp/lamroger/newfile
stat -f .
ls -la


#unmount and remount, then show data
cd /tmp
fusermount -u /tmp/lamroger
# launch our file system
cd $cwd
./a1fs disk /tmp/lamroger
#the data should still be there
cd /tmp/lamroger
stat -f . 
ls -la

#read contents of newfile
cat /tmp/lamroger/newfile