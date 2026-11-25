package com.novahd.recomp.model;

import java.util.List;
import java.util.Map;

public class ResourceManifest {
    public String version;
    public long totalSize;
    public long requiredFreeSpace;
    public List<ResourceFile> files;
    public Map<String, String> metadata;

    public static class ResourceFile {
        public String path;
        public long size;
        public String sha256;
        public String url;
        public boolean required;
        public String compression; // "none", "gzip", "zstd"
        
        public String getFullUrl(String baseUrl) {
            if (url != null && !url.isEmpty()) {
                return url;
            }
            return baseUrl + path;
        }
    }

    public long getTotalRequiredSize() {
        long sum = 0;
        if (files != null) {
            for (ResourceFile f : files) {
                if (f.required) sum += f.size;
            }
        }
        return sum;
    }

    public ResourceFile findByPath(String path) {
        if (files != null) {
            for (ResourceFile f : files) {
                if (f.path.equals(path)) return f;
            }
        }
        return null;
    }
}
