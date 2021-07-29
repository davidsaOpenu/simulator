# Parse ssd configuration
#import sys

MAX_POWER = 16

with open('ssd.conf', 'r') as f:
	data=dict(x.strip().split(' ', 1) for x in f.readlines())

with open('template.fio', 'r') as f:
	template = f.read()

def g(name): return int(data[name])

PAGE_NB = g('PAGE_NB')
PAGE_SIZE = g('PAGE_SIZE')
BLOCK_SIZE = PAGE_NB * PAGE_SIZE

# Put the calculated block size into the template
template = template.replace('<BLOCK_SIZE>', str(BLOCK_SIZE))

# Generate a test for each test case
for mode in [ 'r', 'w', 'rw' ]:
	for i in range(MAX_POWER+1):
		num_blocks = 2**i
		test_id = mode + '_' + str(i)
		f_name = 'output/' + test_id

		test_data = template.replace('<ID>', test_id)
		test_data = test_data.replace('<MODE>', mode)
		test_data = test_data.replace('<TEST_SIZE>', str(num_blocks * BLOCK_SIZE))

		with open(f_name, 'w') as f:
			f.write(test_data)