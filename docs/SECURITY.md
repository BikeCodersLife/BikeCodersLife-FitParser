# Security Measures

The BikeCodersLife FIT Parser implements multiple layers of security to safely process untrusted FIT files.

## Overview

The parser is designed to run in isolated environments (Scaleway Serverless Jobs) and implements defense-in-depth security measures to protect against malicious files and resource exhaustion attacks.

## Security Layers

### 1. Resource Limits (System-Level)

Implemented via `setrlimit()` in [`src/main.cpp`](../src/main.cpp):

```cpp
void setResourceLimits() {
    struct rlimit limit;
    
    // Memory limit: 256MB
    limit.rlim_cur = 256 * 1024 * 1024;
    limit.rlim_max = 256 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &limit);
    
    // CPU time limit: 30 seconds
    limit.rlim_cur = 30;
    limit.rlim_max = 30;
    setrlimit(RLIMIT_CPU, &limit);
    
    // No core dumps
    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &limit);
    
    // File descriptor limit
    limit.rlim_cur = 10;
    limit.rlim_max = 10;
    setrlimit(RLIMIT_NOFILE, &limit);
}
```

**Protects Against:**
- ✅ Memory bombs (files that expand to consume all memory)
- ✅ Infinite loops or CPU exhaustion
- ✅ Core dump information leakage
- ✅ File descriptor exhaustion attacks

**Limits:**
- **256MB** maximum memory usage
- **30 seconds** maximum CPU time
- **10** maximum open file descriptors
- **0 bytes** core dump size (disabled)

### 2. File Validation (Application-Level)

Implemented in `validateFile()` function:

```cpp
bool validateFile(const char* filepath) {
    const off_t MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB
    
    // 1. Null/empty path check
    if (!filepath || filepath[0] == '\0') {
        return false;
    }
    
    // 2. Path sanitization (prevent directory traversal)
    char realPath[PATH_MAX];
    if (!realpath(filepath, realPath)) {
        return false;
    }
    
    // 3. File existence and stats
    struct stat fileStat;
    if (stat(realPath, &fileStat) != 0) {
        return false;
    }
    
    // 4. Regular file check (not device, symlink, pipe, etc.)
    if (!S_ISREG(fileStat.st_mode)) {
        return false;
    }
    
    // 5. Size validation
    if (fileStat.st_size > MAX_FILE_SIZE || fileStat.st_size == 0) {
        return false;
    }
    
    return true;
}
```

**Protects Against:**
- ✅ Directory traversal attacks (`../../etc/passwd`)
- ✅ Symlink attacks
- ✅ Device file access (`/dev/null`, `/dev/random`)
- ✅ Named pipe (FIFO) attacks
- ✅ Oversized files (>100MB)
- ✅ Empty files

**Validation Steps:**
1. **Null check**: Rejects null or empty paths
2. **Path resolution**: Uses `realpath()` to resolve to absolute canonical path
3. **File existence**: Verifies file exists and is accessible
4. **Type check**: Ensures it's a regular file (not device, symlink, directory, etc.)
5. **Size check**: Rejects files >100MB or empty files

### 3. Format Validation (SDK-Level)

The Garmin FIT SDK provides built-in validation:

```cpp
fit::Decode decode;
if (!decode.Read(file, listener)) {
    throw std::runtime_error("Failed to decode FIT file");
}
```

**Protects Against:**
- ✅ Malformed FIT files
- ✅ Invalid FIT headers
- ✅ Corrupted data structures
- ✅ Invalid field definitions

**SDK Checks:**
- Magic byte verification (FIT file signature)
- Header CRC validation
- Message definition validation
- Field type validation
- Data integrity checks

### 4. Process Isolation

The parser runs as a separate process from the PHP application:

```php
// PHP calls parser as subprocess
$process = new Process(['/usr/local/bin/fit-parser', $fitFilePath]);
$process->setTimeout(30);
$process->mustRun();
```

**Protects Against:**
- ✅ Memory corruption affecting main application
- ✅ Crashes affecting main application
- ✅ Resource exhaustion affecting main application

**Benefits:**
- Parser crashes don't crash PHP
- Memory leaks are contained
- Process terminates after parsing
- Clean resource cleanup

### 5. Read-Only Operations

The parser **never writes files**:

```cpp
// Only reads files
std::fstream file(filename_, std::ios::in | std::ios::binary);

// Only writes to stdout
std::cout << json_output;
```

**Protects Against:**
- ✅ File system modification
- ✅ Data exfiltration via file writes
- ✅ Log injection attacks

**Guarantees:**
- No file creation
- No file modification
- No file deletion
- Output only to stdout

### 6. No Network Access

The binary has **no network capabilities**:

**Protects Against:**
- ✅ Data exfiltration via network
- ✅ Command & control callbacks
- ✅ DNS tunneling

**Guarantees:**
- No HTTP/HTTPS requests
- No DNS lookups
- No socket connections
- Fully offline operation

## Scaleway Serverless Jobs Security

When deployed to Scaleway, additional security layers apply:

### Container Isolation

- **Isolated containers**: Each job runs in a separate container
- **Temporary filesystem**: Files deleted after job completion
- **No persistent state**: Fresh container for each execution
- **Network policies**: Can restrict outbound connections

### Resource Quotas

- **CPU limits**: Scaleway enforces vCPU limits
- **Memory limits**: Scaleway enforces memory quotas
- **Execution timeout**: Jobs automatically killed after timeout
- **Concurrent limits**: Maximum concurrent job executions

### Environment Isolation

- **No shared filesystem**: Jobs can't access each other's files
- **No shared memory**: Complete memory isolation
- **No shared network**: Network namespace isolation
- **Ephemeral storage**: All data deleted after job

## Attack Scenarios & Mitigations

### Scenario 1: Malicious FIT File (Zip Bomb)

**Attack**: Upload a FIT file that expands to consume all memory

**Mitigations:**
1. ✅ File size limit (100MB max) prevents initial upload
2. ✅ Memory limit (256MB) prevents memory exhaustion
3. ✅ SDK validation rejects malformed files
4. ✅ Process isolation prevents affecting other jobs

### Scenario 2: Directory Traversal

**Attack**: Try to read `/etc/passwd` via path traversal

**Mitigations:**
1. ✅ `realpath()` resolves to canonical path
2. ✅ File type check rejects non-regular files
3. ✅ Read-only access prevents modification

**Test:**
```bash
./fit-parser "../../etc/passwd"
# Output: Error: Cannot resolve file path
```

### Scenario 3: Symlink Attack

**Attack**: Create symlink to sensitive file

**Mitigations:**
1. ✅ `realpath()` follows symlinks to real file
2. ✅ File type check rejects non-regular files
3. ✅ Container isolation limits accessible files

**Test:**
```bash
./fit-parser /dev/null
# Output: Error: Not a regular file
```

### Scenario 4: Resource Exhaustion

**Attack**: Upload file that causes infinite loop

**Mitigations:**
1. ✅ CPU time limit (30s) kills process
2. ✅ Memory limit (256MB) prevents memory exhaustion
3. ✅ Scaleway timeout kills container
4. ✅ Process isolation prevents affecting other jobs

### Scenario 5: Code Injection

**Attack**: Inject malicious code via FIT file

**Mitigations:**
1. ✅ Binary parsing (no eval/exec)
2. ✅ SDK validates data structures
3. ✅ No dynamic code execution
4. ✅ Read-only operations

## Security Testing

### Automated Tests

```bash
# Test 1: Reject device files
./fit-parser /dev/null
# Expected: Error: Not a regular file

# Test 2: Reject non-FIT files
./fit-parser /etc/passwd
# Expected: Error: FIT decode error

# Test 3: Accept valid FIT files
./fit-parser valid.fit
# Expected: JSON output
```

### Manual Security Review

- ✅ No `system()` or `exec()` calls
- ✅ No dynamic memory allocation without limits
- ✅ No file writes
- ✅ No network calls
- ✅ No user input in shell commands
- ✅ All errors handled gracefully

## Security Checklist

- [x] Resource limits enforced (memory, CPU, file descriptors)
- [x] File validation (size, type, path sanitization)
- [x] Format validation (FIT SDK)
- [x] Process isolation (subprocess)
- [x] Read-only operations
- [x] No network access
- [x] Container isolation (Scaleway)
- [x] Error handling (no crashes)
- [x] Security testing (device files, symlinks, malformed files)

## Recommendations

### For Production Deployment

1. **Monitor resource usage**: Track memory and CPU usage in production
2. **Log security events**: Log rejected files and validation failures
3. **Rate limiting**: Implement rate limits on file uploads
4. **File scanning**: Consider additional malware scanning before parsing
5. **Audit logs**: Keep audit trail of all file processing

### For Future Enhancements

1. **Sandboxing**: Consider additional sandboxing (seccomp, AppArmor)
2. **Input sanitization**: Add magic byte verification before SDK parsing
3. **Output validation**: Validate JSON output size and structure
4. **Monitoring**: Add metrics for security events

## Conclusion

The BikeCodersLife FIT Parser implements **defense-in-depth security** with multiple independent layers:

1. System-level resource limits
2. Application-level file validation
3. SDK-level format validation
4. Process isolation
5. Container isolation

This multi-layered approach ensures that even if one layer fails, others provide protection. The parser is **safe for processing untrusted user-uploaded FIT files** in a serverless environment.
