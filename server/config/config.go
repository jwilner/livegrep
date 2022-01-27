package config

import (
	"html/template"
	"time"
)

type Backend struct {
	Id   string `json:"id"`
	Addr string `json:"addr"`
}

type Honeycomb struct {
	WriteKey string `json:"write_key"`
	Dataset  string `json:"dataset"`
}

type GoogleIAPConfig struct {
	ProjectNumber string `json:"project_number"`

	// Use BackendServiceID for GKE and GCE
	BackendServiceID string `json:"backend_service_id"`

	// Use ProjectID for GCE
	ProjectID string `json:"project_id"`

	// Server will set this
	Aud string
}

// Single cache support only atm
// https://pkg.go.dev/github.com/go-redis/redis/v8#Options
type RedisCacheConfig struct {
	Network   string        `json:"network"` // tcp|unix
	Addr      string        `json:"addr"`    // host:port
	Password  string        `json:"password"`
	KeyTTL    string        `json:"key_ttl"`
	KeyTTLD   time.Duration `json:"key_ttl_d"` // value of time.Duration.Parse(KeyTTL)
	KeyPrefix string        `json:"key_prefix"`
}

type Config struct {
	// Location of the directory containing templates and static
	// assets. This should point at the "web" directory of the
	// repository.
	DocRoot string `json:"docroot"`

	Feedback struct {
		// The mailto address for the "feedback" url.
		MailTo string `json:"mailto"`
	} `json:"feedback"`

	GoogleAnalyticsId string `json:"google_analytics_id"`
	// Should we respect X-Real-Ip, X-Real-Proto, and X-Forwarded-Host?
	ReverseProxy bool `json:"reverse_proxy"`

	// List of backends to connect to. Each backend must include
	// the "id" and "addr" fields.
	Backends []Backend `json:"backends"`

	// The address to listen on, as HOST:PORT.
	Listen string `json:"listen"`

	// HTML injected into layout template
	// for site-specific customizations
	HeaderHTML template.HTML `json:"header_html"`

	// HTML injected into layout template
	// just before </body> for site-specific customization
	FooterHTML template.HTML `json:"footer_html"`

	Sentry struct {
		URI string `json:"uri"`
	} `json:"sentry"`

	// Whether to re-load templates on every request
	Reload bool `json:"reload"`

	// honeycomb API write key
	Honeycomb Honeycomb `json:"honeycomb"`

	// If configured, requests will have their headers
	// validated to make sure they are coming from IAP
	GoogleIAPConfig GoogleIAPConfig `json:"google_iap_config"`

	DefaultMaxMatches int32 `json:"default_max_matches"`

	// Same json config structure that the backend uses when building indexes;
	// used here for repository browsing.
	IndexConfig IndexConfig `json:"index_config"`

	DefaultSearchRepos []string `json:"default_search_repos"`

	LinkConfigs []LinkConfig `json:"file_links"`

	RedisCacheConfig RedisCacheConfig `json:"redis_cache_config"`
}

type IndexConfig struct {
	Name         string       `json:"name"`
	Repositories []RepoConfig `json:"repositories"`
}

type RepoConfig struct {
	Path           string            `json:"path"`
	Name           string            `json:"name"`
	Revisions      []string          `json:"revisions"`
	Metadata       map[string]string `json:"metadata"`
	WalkSubmodules bool              `json:"walk_submodules"`
}

type LinkConfig struct {
	Label            string `json:"label"`
	UrlTemplate      string `json:"url_template"`
	WhitelistPattern string `json:"whitelist_pattern"`
}
