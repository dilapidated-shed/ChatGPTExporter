package org.isomorphisms.chatgptprobe;

import android.app.Activity;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Deliberately small instrumented browser used only to observe chatgpt.com's
 * private web contract. No exporter logic belongs here.
 */
public final class MainActivity extends Activity {
    private static final String HOME = "https://chatgpt.com/";

    private WebView webView;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // The probe exists to create a Chromium/WebView DevTools endpoint we own.
        WebView.setWebContentsDebuggingEnabled(true);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER_VERTICAL);

        Button home = new Button(this);
        home.setText("ChatGPT");
        home.setOnClickListener(v -> webView.loadUrl(HOME));
        controls.addView(home);

        Button reload = new Button(this);
        reload.setText("Reload");
        reload.setOnClickListener(v -> webView.reload());
        controls.addView(reload);

        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setSingleLine(true);
        status.setPadding(12, 0, 8, 0);
        controls.addView(status, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        root.addView(controls, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        webView = new WebView(this);
        configureWebView(webView);
        root.addView(webView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
        updateStatus(HOME);
        webView.loadUrl(HOME);
    }

    private void configureWebView(WebView view) {
        WebSettings settings = view.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            settings.setSafeBrowsingEnabled(true);
        }

        CookieManager cookies = CookieManager.getInstance();
        cookies.setAcceptCookie(true);
        cookies.setAcceptThirdPartyCookies(view, true);

        view.setWebChromeClient(new WebChromeClient());
        view.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                // Keep HTTPS navigation, including login redirects, inside the probe.
                // There is deliberately no JavaScript-to-native bridge.
                return false;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                updateStatus(url);
            }
        });
    }

    private void updateStatus(String url) {
        String provider = "WebView";
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            PackageInfo info = WebView.getCurrentWebViewPackage();
            if (info != null) provider = info.packageName + " " + info.versionName;
        }
        status.setText(provider + "  •  " + url);
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.stopLoading();
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }
}
