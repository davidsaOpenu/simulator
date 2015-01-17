#!/bin/bash
#
# Run things.  This script was used to generate the OSD Target paper
# submitted to MSST07.
#

list() {
	for zeroes in "" ; do
	    for o in 1 2 3 4 6 ; do
		    ./list -o $o$zeroes -i 10000
	    done
	done
	for zeroes in 0 00 000 ; do
	    for o in 1 3 ; do
		    ./list -o $o$zeroes -i 100
	    done
	done
	for zeroes in 0000 00000 ; do
	    for o in 1 3 ; do
		    ./list -o $o$zeroes -i 30
	    done
	done
	# results get very odd beyond 30k
	for zeroes in 000000 0000000 ; do
	    for o in 1 3 ; do
		    ./list -o $o$zeroes -i 10
	    done
	done
}

listattr() {
	for attr in 10 100 ; do
		for retrieve in 1 5 10 ; do
			for o in 1 2 3 4 6 ; do
			    ./list -i 1000 -o $o -a $attr -r $retrieve
			done
			for o in 10 30 100 300 ; do
			    ./list -i 100 -o $o -a $attr -r $retrieve
			done
			# while a good amount of time is in the create,
			# the operations take long too, so drop the iters
			for o in 1000 3000 ; do
			    iter=100
			    if (( attr == 100 )) ; then
				iter=10
			    fi
			    ./list -i $iter -o $o -a $attr -r $retrieve
			done
			for o in 10000 30000 ; do
			    ./list -i 10 -o $o -a $attr -r $retrieve
			done
			echo
			echo
		done
	done
}

# both query and sma are flat with respect to total device objects, so
# just set numobj == numobj_in_coll.
query() {
    (
    criteria=2
    attr=10
    echo "# fixed criteria=$criteria, attr=$attr; variable match"
    for match in 1 10 30 100 300 1000; do
	for obj in 10 30 100 300 1000 3000 10000 ; do
	    iter=10
	    ./query -i $iter -o $obj -c $obj -m $match \
		    -a $attr -n $criteria
	done
	echo
	echo
    done
    ) > query-match.out

    (
    match=10
    attr=10
    echo "# fixed match=$match, attr=$attr; variable criteria"
    for criteria in 1 2 4 7 10 ; do
	for obj in 10 30 100 300 1000 3000 10000 ; do
	    iter=10
	    ./query -i $iter -o $obj -c $obj -m $match \
		    -a $attr -n $criteria
	done
	echo
	echo
    done
    ) > query-criteria.out

    (
    criteria=2
    match=10
    echo "# fixed criteria=$criteria, match=$match; variable attr"
    for attr in 2 20 200 ; do
	for obj in 10 30 100 300 1000 3000 10000 ; do
	    iter=10
	    ./query -i $iter -o $obj -c $obj -m $match \
		    -a $attr -n $criteria
	done
	echo
	echo
    done
    ) > query-attr.out
}

sma() {
    (
    numset=1
    echo "# fixed set=$numset; variable attr"
    for attr in 2 20 40 80 ; do
	for obj in 10 30 100 300 1000 3000 10000 ; do
	    iter=100
	    ./set_member_attributes -i $iter -o $obj -c $obj \
				    -a $attr -s $numset
	done
	echo
	echo
    done
    ) > sma-attr.out

    (
    attr=10
    echo "# fixed attr=$attr; variable set"
    for numset in 1 2 3 5 10 ; do
	for obj in 10 30 100 300 1000 3000 10000 ; do
	    iter=100
	    if (( obj == 10000 )) ; then
		iter=10
	    fi
	    ./set_member_attributes -i $iter -o $obj -c $obj \
				    -a $attr -s $numset
	done
	echo
	echo
    done
    ) > sma-set.out
}

sma_attr_trans() {
    numset=1
    for obj in 10 100 1000 ; do
	for attr in 1 10 20 30 40 50 60 70 80 ; do
	    iter=100
	    ./set_member_attributes -i $iter -o $obj -c $obj \
				    -a $attr -s $numset
	done
	echo
	echo
    done
}

sma_set_trans() {
    attr=10
    for obj in 10 100 1000 ; do
	for ((numset=1; numset<=10; numset++)) ; do
	    iter=100
	    ./set_member_attributes -i $iter -o $obj -c $obj \
				    -a $attr -s $numset
	done
	echo
	echo
    done
}

#list > list.out
#listattr > listattr.out
query
#sma
#sma_attr_trans > sma-attr-trans.out
#sma_set_trans > sma-set-trans.out

