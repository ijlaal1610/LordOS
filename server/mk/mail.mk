# Mail, Calendar, Contacts and Reminders (docs/MAIL.md). lord-maild is the sandboxed helper that
# speaks IMAP, SMTP, CalDAV and CardDAV; with lord-sessiond it is the only binary linking OpenSSL.
MAIL_PARSE := src/mail/xml.c src/mail/mime.c src/mail/html.c src/mail/imapp.c src/mail/ical.c src/mail/vcard.c
MAIL_HDR := $(wildcard src/mail/*.h) src/doc/wire.h src/sec/json.h src/ui/tzif.h
# parsers: IMAP, MIME, HTML sanitizer, iCalendar, vCard and XML, with a small fuzzer (tests/mail_test.c)
test: build/mail-test
build/mail-test: tests/mail_test.c $(MAIL_PARSE) $(MAIL_HDR) src/doc/wire.c src/sec/json.c src/ui/tzif.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/mail_test.c $(MAIL_PARSE) src/doc/wire.c src/sec/json.c src/ui/tzif.c -lm
	./$@ || { rm -f $@; exit 1; }
# the same under AddressSanitizer + UBSan (not part of `test`: slower)
build/mail-test-asan: tests/mail_test.c $(MAIL_PARSE) $(MAIL_HDR) src/doc/wire.c src/sec/json.c src/ui/tzif.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) -std=c11 -o $@ tests/mail_test.c $(MAIL_PARSE) src/doc/wire.c src/sec/json.c src/ui/tzif.c -lm
	./$@
.PHONY: mail-asan
mail-asan: build/mail-test-asan
# lord-server's side: start lord-maild, its channel, the connector (allowed servers only) and lord.mail
MAILC_OBJ := build/obj/mail/mailc.o build/obj/ui/js_mail.o
build/lord-server: $(MAILC_OBJ)
$(MAILC_OBJ): $(wildcard src/mail/*.h) src/ui/js_mail.h
# the connector's policy: allowed endpoints and the public-address check for remote images
test: build/mailc-test
build/mailc-test: tests/mailc_test.c src/mail/mailc.c src/mail/mailc.h src/doc/wire.c src/sec/json.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/mailc_test.c src/mail/mailc.c src/doc/wire.c src/sec/json.c -lpthread -lm
	./$@ || { rm -f $@; exit 1; }
