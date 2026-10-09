# The expected output of t/172-read-http-shaped.slight: the client's three
# requests framed as HTTP/1.1 frames them (header lines to a blank one,
# then Content-Length bytes), the server's answer to each, and those
# answers framed the same way, as the client shows their bodies.

def messages(data):
    out = []
    while data:
        head, _, data = data.partition(b"\r\n\r\n")
        lines = head.split(b"\r\n")
        n = 0
        for h in lines[1:]:
            if h.startswith(b"Content-Length: "):
                n = int(h[16:])
        out.append((lines[0], data[:n]))
        data = data[n:]
    return out

sent = (b"POST /one HTTP/1.1\r\nContent-Length: 11\r\n\r\nhello\nworld"
        b"POST /two HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\na\r\n\r\nb"
        b"GET /three HTTP/1.1\r\n\r\n")
answers = b""
for line, body in messages(sent):
    text = line + b" got " + str(len(body)).encode() + b" bytes: " + body
    answers += b"HTTP/1.1 200 OK\r\nContent-Length: " + str(len(text)).encode() + b"\r\n\r\n" + text
shown = [body.decode().replace("\r", "\\r").replace("\n", "\\n") for _, body in messages(answers)]
print("(ok (ok (" + " ".join('"%s"' % s for s in shown) + ")))")
print("()")
