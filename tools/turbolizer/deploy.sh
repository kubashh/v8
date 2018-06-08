#!/bin/bash

DEST=$1

if [ ! -d "$DEST" ]; then
  echo -e "Destination \"$DEST\" is not a directory. Run\n\tnpm deploy -- [destination-directory]"
  exit 1
fi

DEPLOY=./deploy
rm -rf $DEPLOY
mkdir $DEPLOY

cp *.jpg $DEPLOY/
cp *.png $DEPLOY/
cp *.css $DEPLOY/
cp index.html $DEPLOY/
cp -R build $DEPLOY/

echo "Created $DEPLOY, deploying now ..."

cp -vR $DEPLOY $DEST

echo "Deployed to $1"
