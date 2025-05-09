```bash
# Install dependencies.

# cd docs/EN
pip install -r requirements.txt

# Build the docs.
make clean
make html
sphinx-autobuild source build/html
```