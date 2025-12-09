![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/Digvijay-x1/Search-Engine?utm_source=oss&utm_medium=github&utm_campaign=Digvijay-x1%2FSearch-Engine&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)

## Setup

### Environment Variables

This project uses environment variables for configuration, including database credentials.

1.  Copy the example environment file:
    ```bash
    cp .env.example .env
    ```
2.  Edit `.env` and set your own secure passwords and configuration:
    ```ini
    DB_USER=admin
    DB_PASS=your_secure_password
    DB_NAME=search_engine
    ```

### Running with Docker

```bash
docker-compose up --build
```
