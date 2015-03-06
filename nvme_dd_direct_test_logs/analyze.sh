#/bin/bash

function report(){
	fn=$1
	echo -e '\n'$fn'\n----------------------------------'
	for readwrite in 'Read' 'Write'; do
		cmd="grep "$readwrite"\scmd\scalled $fn"
		count=`$cmd -c`	
		echo " * "$readwrite" "$count
		for sqid in 1 2 3 4; do
			cmd2="grep '"$readwrite"\scmd\scalled' "$fn" -A 4 -B 10 | grep '220:\ssq_id\s"$sqid"' -c"
			#echo $cmd2
			eval dastring=\`${cmd2}\`
			echo -e '  * '"sqid $sqid $dastring"
		done
		for prp in PRP1 PRP2 prp_list; do
			cmd2="grep '"$readwrite"\scmd\scalled' "$fn" -A 4 -B 10 | grep '$prp' -c"
			#echo $cmd2
			eval dastring=\`${cmd2}\`
			echo -e '  * '"$prp $dastring"
		done
	done
}

if [ $# -gt 0 ]; then
	report $1
else
	for fn in `ls dd_*_direct `; do
		report $fn
	done
fi


