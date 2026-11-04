//! Tiny HTTP helpers shared by the DeepL and grammar clients (blocking: call them from a worker thread).

use std::time::Duration;

/// Percent-encodes everything but the unreserved characters (`QUrl::toPercentEncoding`): '+' included.
pub fn percent_encode(text: &str) -> String {
    let mut out = String::with_capacity(text.len() * 3);
    for b in text.bytes() {
        if b.is_ascii_alphanumeric() || matches!(b, b'-' | b'.' | b'_' | b'~') {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

pub fn agent(timeout_secs: u64) -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(timeout_secs))
        .user_agent("FalloutBabylon/0.2")
        .build()
}

/// A server that answers canned responses, for the tests of the clients (nothing leaves the machine).
#[cfg(test)]
pub mod mock {
    use std::collections::HashMap;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::sync::{Arc, Mutex};

    #[derive(Debug, Clone, Default)]
    pub struct Request {
        pub method: String,
        pub path: String,
        pub body: String,
        pub headers: HashMap<String, String>,
    }

    #[derive(Debug, Clone, Default)]
    pub struct Response {
        pub status: u16,
        pub body: String,
    }

    impl Response {
        pub fn json(body: &str) -> Response {
            Response { status: 200, body: body.to_string() }
        }
        pub fn status(status: u16) -> Response {
            Response { status, body: String::new() }
        }
    }

    pub struct MockServer {
        pub url: String,
        pub requests: Arc<Mutex<Vec<Request>>>,
    }

    impl MockServer {
        pub fn start(handler: impl Fn(&Request) -> Response + Send + Sync + 'static) -> MockServer {
            let listener = TcpListener::bind("127.0.0.1:0").unwrap();
            let url = format!("http://127.0.0.1:{}", listener.local_addr().unwrap().port());
            let requests = Arc::new(Mutex::new(Vec::new()));
            let log = requests.clone();
            let handler = Arc::new(handler);
            std::thread::spawn(move || {
                for stream in listener.incoming() {
                    let Ok(mut stream) = stream else { continue };
                    let log = log.clone();
                    let handler = handler.clone();
                    std::thread::spawn(move || {
                        let mut buffer: Vec<u8> = Vec::new();
                        let mut chunk = [0u8; 4096];
                        let (head_end, length) = loop {
                            let n = stream.read(&mut chunk).unwrap_or(0);
                            if n == 0 {
                                return;
                            }
                            buffer.extend_from_slice(&chunk[..n]);
                            if let Some(pos) = buffer.windows(4).position(|w| w == b"\r\n\r\n") {
                                let head = String::from_utf8_lossy(&buffer[..pos]).to_lowercase();
                                let length = head
                                    .lines()
                                    .find_map(|l| l.strip_prefix("content-length:"))
                                    .and_then(|v| v.trim().parse::<usize>().ok())
                                    .unwrap_or(0);
                                break (pos, length);
                            }
                        };
                        while buffer.len() < head_end + 4 + length {
                            let n = stream.read(&mut chunk).unwrap_or(0);
                            if n == 0 {
                                break;
                            }
                            buffer.extend_from_slice(&chunk[..n]);
                        }
                        let head = String::from_utf8_lossy(&buffer[..head_end]).into_owned();
                        let mut lines = head.split("\r\n");
                        let first: Vec<&str> = lines.next().unwrap_or("").split(' ').collect();
                        let mut request = Request {
                            method: first.first().unwrap_or(&"").to_string(),
                            path: first.get(1).unwrap_or(&"").to_string(),
                            ..Default::default()
                        };
                        for l in lines {
                            if let Some((k, v)) = l.split_once(':') {
                                request.headers.insert(k.trim().to_lowercase(), v.trim().to_string());
                            }
                        }
                        request.body = String::from_utf8_lossy(&buffer[head_end + 4..head_end + 4 + length]).into_owned();
                        log.lock().unwrap().push(request.clone());
                        let response = handler(&request);
                        let status = if response.status == 0 { 200 } else { response.status };
                        let text = format!(
                            "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                            status,
                            response.body.len(),
                            response.body
                        );
                        let _ = stream.write_all(text.as_bytes());
                    });
                }
            });
            MockServer { url, requests }
        }

        pub fn count(&self) -> usize {
            self.requests.lock().unwrap().len()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encoding() {
        assert_eq!(percent_encode("a+b & c é"), "a%2Bb%20%26%20c%20%C3%A9");
        assert_eq!(percent_encode("fr-FR_1.~"), "fr-FR_1.~");
    }
}
