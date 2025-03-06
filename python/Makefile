python: src/__main__.py
	zip -j sb.zip src/*
	echo '#!/usr/bin/env python3' | cat - sb.zip > sb
	chmod a+x sb
	rm sb.zip
