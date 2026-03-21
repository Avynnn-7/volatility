# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 2.0.x   | :white_check_mark: |
| 1.0.x   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability in vol_arb, please report it
responsibly:

1. **Do NOT** open a public GitHub issue
2. Email security concerns to: [project maintainer email]
3. Include:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Suggested fix (if any)

## Response Timeline

- **Acknowledgment**: Within 48 hours
- **Initial Assessment**: Within 7 days
- **Resolution**: Depends on severity
  - Critical: 7 days
  - High: 14 days
  - Medium: 30 days
  - Low: 60 days

## Security Best Practices

When using vol_arb in production:

### Input Validation

All user inputs are validated, but additional checks are recommended:
- Validate file paths before loading
- Sanitize JSON inputs from external sources
- Limit maximum data sizes

### Configuration

- Store configuration files with restricted permissions
- Do not commit credentials to version control
- Use environment variables for sensitive settings

### API Security

When using the REST API:
- Run behind a reverse proxy (nginx, etc.)
- Enable HTTPS in production
- Implement rate limiting
- Add authentication for sensitive endpoints

### Logging

- Review log files for anomalies
- Do not log sensitive data (credentials, full data dumps)
- Rotate logs regularly

## Known Limitations

- No built-in authentication (implement at application layer)
- File operations use system permissions (ensure proper access control)
- Memory pool does not sanitize deallocated memory
