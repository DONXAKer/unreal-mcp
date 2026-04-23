FROM python:3.12-slim

WORKDIR /app

# Install uv for fast dependency management
RUN pip install uv --no-cache-dir

# Copy dependency files
COPY Python/pyproject.toml Python/uv.lock ./

# Install dependencies
RUN uv sync --frozen --no-dev

# Copy server source
COPY Python/ .

EXPOSE 3001

ENV UNREAL_HOST=host.docker.internal \
    UNREAL_PORT=55557 \
    MCP_HTTP_PORT=3001

CMD ["uv", "run", "python", "unreal_mcp_server_http.py"]
