#!/bin/sh
NARGS=6
THOST=$1
TPORT=$2
TMASTER_ID=$3
TUSER=$4
TPWD=$5
DIR=$6

if [ $# != $NARGS ] ;
then
echo""
echo "Wrong number of arguments, gave "$#" but "$NARGS" is required"
echo "" 
echo "Usage :" 
echo "        rwsplit.sh <log filename> <host> <port> <master id> <user> <password>"
echo ""
exit 1
fi


RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent

TINPUT=$DIR/test_transaction_routing2.sql
TRETVAL=0
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_transaction_routing2b.sql
TRETVAL=0
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_transaction_routing3.sql
TRETVAL=2
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TMASTER_ID" ]; then 
        echo "$TINPUT FAILED, return value $a when one of the slave IDs was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_transaction_routing3b.sql
TRETVAL=2
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TMASTER_ID" ]; then 
        echo "$TINPUT FAILED, return value $a when one of the slave IDs was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

# test implicit transaction, that is, not started explicitly, autocommit=0
TINPUT=$DIR/test_transaction_routing4.sql
TRETVAL=0
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_transaction_routing4b.sql
TRETVAL=0
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

# set a var via SELECT INTO @, get data from master, returning server-id: put master server-id value in TRETVAL
TINPUT=$DIR/select_for_var_set.sql
TRETVAL=$TMASTER_ID

a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_implicit_commit1.sql
TRETVAL=$TMASTER_ID

a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_implicit_commit2.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_implicit_commit3.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_implicit_commit4.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=test_implicit_commit5.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_implicit_commit6.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_implicit_commit7.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi

TINPUT=$DIR/test_autocommit_disabled1.sql
TRETVAL=1
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi

TINPUT=$DIR/test_autocommit_disabled1b.sql
TRETVAL=1
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi

# Disable autocommit in the first session and then test in new session that 
# it is again enabled.
TINPUT=$DIR/test_autocommit_disabled2.sql
TRETVAL=1
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi

TINPUT=$DIR/set_autocommit_disabled.sql
`$RUNCMD < $TINPUT`
TINPUT=test_after_autocommit_disabled.sql
TRETVAL=$TMASTER_ID
a=`$RUNCMD < $TINPUT`
if [ "$a" = "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when it was not accetable"; 
else 
        echo "$TINPUT PASSED" ; 
fi


TINPUT=$DIR/test_sescmd.sql
TRETVAL=2
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi
a=`$RUNCMD < $TINPUT`
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi

TINPUT=$DIR/test_temporary_table.sql
a=`$RUNCMD < $TINPUT`
TRETVAL=1
if [ "$a" != "$TRETVAL" ]; then
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected";
else
        echo "$TINPUT PASSED" ;
fi

echo "-----------------------------------" 
echo "Session variables: Stress Test 1" 
echo "-----------------------------------" 

RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ -q\ -r
TINPUT=$DIR/test_sescmd2.sql
for ((i = 0;i<1000;i++))
do
    a=`$RUNCMD < $TINPUT 2>&1`
    if [[ "`echo "$a"|grep -i 'error'`" != "" ]]
    then
	err=`echo "$a" | grep -i error`
	break
    fi
done
if [[ "$err" == "" ]]
then
    echo "TEST PASSED" 
else
    echo "$err" 
    echo "Test FAILED at iteration $((i+1))" 
fi
echo "-----------------------------------" 
echo "Session variables: Stress Test 2" 
echo "-----------------------------------" 
echo ""
err=""
TINPUT=$DIR/test_sescmd3.sql
for ((j = 0;j<1000;j++))
do
    b=`$RUNCMD < $TINPUT 2>&1`
    if [[ "`echo "$b"|grep -i 'null\|error'`" != "" ]]
    then
	err=`echo "$b" | grep -i null\|error`
	break
    fi
done
if [[ "$err" == "" ]]
then
    echo "TEST PASSED" 
else
    echo "Test FAILED at iteration $((j+1))" 
fi
echo "" 
