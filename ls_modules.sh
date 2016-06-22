#!/bin/sh

ROOT_DIR=`dirname $0`
ROOT_DIR=`cd $ROOT_DIR ; pwd`

if [ -f $K1_TOOLCHAIN_DIR/../k1Req/aci/utils/ls_modules.rb ]; then
    $K1_TOOLCHAIN_DIR/../k1Req/aci/utils/ls_modules.rb $*;
else
    echo "$K1_TOOLCHAIN_DIR/../k1Req/aci/utils/ls_modules.rb does not exist.";
    exit 0;
fi
