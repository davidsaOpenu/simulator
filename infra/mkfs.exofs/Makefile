mkfs.exofs: main.o exofs.o nvme.o
	$(CC) -o $@ $^

.PHONY: clean
clean:
	rm -rf *.o mkfs.exofs