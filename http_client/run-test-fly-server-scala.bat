for %%w in (*.json.zlib) do wget http://192.168.1.76:37015/fly-zget -d -S --post-file=%%w --output-document=result-last.json.zlib.result
