man_MANS = shairport-sync.1

if USE_XMLTOMAN
shairport-sync.1: shairport-sync.1.xml
	xmltoman $< > $@

# It seems that xmlmantohtml is not working anymore (20240905)
# all-local: shairport-sync.html


# shairport-sync.html: shairport-sync.1.xml
# 	xmlmantohtml $< > $@

clean:
	rm -f shairport-sync.1
# 	rm -f shairport-sync.html
endif