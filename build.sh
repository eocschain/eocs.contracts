#! /bin/bash

<<<<<<< HEAD
printf "\t=========== Building eocs.contracts ===========\n\n"
=======
printf "\t=========== Building eosio.contracts ===========\n\n"
>>>>>>> otherb

RED='\033[0;31m'
NC='\033[0m'

CORES=`getconf _NPROCESSORS_ONLN`
mkdir -p build
pushd build &> /dev/null
cmake ../
make -j${CORES}
popd &> /dev/null
