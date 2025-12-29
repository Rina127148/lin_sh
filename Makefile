compile:
	g++ -std=c++11 -Wall -Wextra -o kubsh kubsh.cpp

deb: compile
	mkdir -p kubsh_1.0-1_amd64/usr/local/bin
	mkdir -p kubsh_1.0-1_amd64/DEBIAN
	cp kubsh kubsh_1.0-1_amd64/usr/local/bin/
	echo "Package: kubsh" > kubsh_1.0-1_amd64/DEBIAN/control
	echo "Version: 1.0" >> kubsh_1.0-1_amd64/DEBIAN/control
	echo "Section: utils" >> kubsh_1.0-1_amd64/DEBIAN/control
	echo "Priority: optional" >> kubsh_1.0-1_amd64/DEBIAN/control
	echo "Architecture: amd64" >> kubsh_1.0-1_amd64/DEBIAN/control
	echo "Depends: libc6 (>= 2.34)" >> kubsh_1.0-1_amd64/DEBIAN/control
	echo "Maintainer: rina <rina@localhost>" >> kubsh_1.0-1_amd64/DEBIAN/control
	echo "Description: Custom shell with VFS for user information" >> kubsh_1.0-1_amd64/DEBIAN/control
	dpkg-deb --build kubsh_1.0-1_amd64

clean:
	rm -f kubsh
	rm -rf kubsh_1.0-1_amd64 kubsh_1.0-1_amd64.deb
