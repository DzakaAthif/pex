cd tests/E2E
TOTAL=0
CORRECT=0
INCORRECT=0
echo "------------------------------------"
for FILENAME in *.temp
do  
    VAR=""
    VAR=${FILENAME%.*}
    ((TOTAL+=1))

    RET=$(diff $VAR.temp $VAR.out)
    if [[ "$RET" == "" ]]
    then
        echo "Testing $VAR.temp against $VAR.out -> Success!"
        ((CORRECT+=1))
    else
        echo "Testing $VAR.temp against $VAR.out -> Failed!"
        ((INCORRECT+=1))
    fi
done
echo "------------------------------------"
if (($INCORRECT == 0))
then
    echo "Passed all $TOTAL test cases."
else
    echo "Failed $INCORRECT out of $TOTAL test cases."
fi
echo "------------------------------------"
rm -f *.temp
cd ../..
cat cov.txt
echo "------------------------------------"