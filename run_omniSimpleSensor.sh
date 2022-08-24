#!/bin/bash

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

echo Running script in ${SCRIPT_DIR}
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${SCRIPT_DIR}/_build/linux-x86_64/release"

pushd $SCRIPT_DIR > /dev/null

echo Hello Script
./_build/linux-x86_64/release/omniSimpleSensor "$@"

if [ $? -eq 0 ] 
then 
  #for i in 0{0..$2}
  i=0
  while [ $i -lt $2 ]
  do
    ./_build/linux-x86_64/release/omniSensorThread $1 $i $3 &
    ((i++))
  done 
fi

popd > /dev/null
