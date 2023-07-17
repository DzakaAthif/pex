files="pe_exchange.c pe_exchange.h pe_trader.h trader1.c pe_common.h Makefile products.txt"
cp $files tests/E2E
cd tests/E2E
make build2
TOTAL=0
echo "------------------------------------"
for FILENAME in *.in
do 
    VAR=${FILENAME%.*}
    echo "Running $VAR test and creating $VAR.temp"
    cp $FILENAME trader1_cmd.txt
    ./pe_exchange products.txt ./trader1 > $VAR.temp
    ((TOTAL+=1))
done
echo "------------------------------------"
echo "$TOTAL test cases."
echo "------------------------------------"
gcov pe_exchange.c > cov.txt
mv cov.txt pe_exchange.c.gcov ../../
rm -f $files trader1_cmd.txt pe_exchange*
cd ../..