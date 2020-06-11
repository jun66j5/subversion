#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#

# Required version of Python
case $1 in
  -2)
    CANDIDATE="$PYTHON $PYTHON2 python python2"
    MIN_VER=${2:-0x2070000}
    MAX_VER="0x3000000"
    break
    ;;
  -3)
    CANDIDATE="$PYTHON $PYTHON3 python python3"
    MIN_VER=${2:-0x3000000}
    MAX_VER="0xffffffff"
    ;;
  *)
    CANDIDATE="$PYTHON $PYTHON3 python python3 $PYTHON2 python2"
    MIN_VER=${1:-0x2070000}
    MAX_VER="0xffffffff"
esac

for pypath in $CANDIDATE; do
  if [ "x$pypath" != "x" ]; then
    DETECT_PYTHON="import sys;\
                   sys.exit(0 if $MIN_VER <= sys.hexversion < $MAX_VER else 1)"
    if "$pypath" -c "$DETECT_PYTHON" >/dev/null 2>/dev/null; then
      echo $pypath
      exit 0
    fi
  fi
done
exit 1
