all: shairport-sync.7 shairport-sync.html

%.7: %.7.xml
	xmltoman $*.7.xml > $*.tmp && mv $*.tmp $*.7

%.html: %.7.xml
	xsltproc xmltoman.xsl $*.7.xml > $*.tmp && mv $*.tmp $*.html

clean:
	rm shairport-sync.7
	rm shairport-sync.html
