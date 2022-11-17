OPENSIPS="bin/opensips"
OPENSIPS_FULL_VERSION=$(strings $OPENSIPS | grep -e '^[a-z]\+ [[:digit:]]\+\.[[:digit:]]\+\.[[:digit:]]\+')
OPENSIPS_COMPILE_FLAGS=$(strings $OPENSIPS | grep -e 'STATS: O')

echo "#define OPENSIPS_FULL_VERSION \"$OPENSIPS_FULL_VERSION\"" > bindgen_extra.h
echo "#define OPENSIPS_COMPILE_FLAGS \"$OPENSIPS_COMPILE_FLAGS\"" >> bindgen_extra.h

bindgen \
    bindgen.h \
    --allowlist-type module_exports \
    --allowlist-var ALL_ROUTES \
    --allowlist-var REQUEST_ROUTE \
    --allowlist-var FAILURE_ROUTE \
    --allowlist-var ONREPLY_ROUTE \
    --allowlist-var BRANCH_ROUTE \
    --allowlist-var ERROR_ROUTE \
    --allowlist-var LOCAL_ROUTE \
    --allowlist-var STARTUP_ROUTE \
    --allowlist-var TIMER_ROUTE \
    --allowlist-var EVENT_ROUTE \
    --allowlist-var NAME \
    --allowlist-var VERSION \
    --allowlist-var 'OPENSIPS_FULL_VERSION.*' \
    --allowlist-var 'OPENSIPS_COMPILE_FLAGS.*' \
    --allowlist-function 'pv_.*' \
    > opensips-mod-sys/src/sys.rs