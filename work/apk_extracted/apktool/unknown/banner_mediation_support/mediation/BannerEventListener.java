/**
 * Fyber Android SDK
 * <p/>
 * Copyright (c) 2015 Fyber. All rights reserved.
 */
package com.fyber.ads.banners.mediation;

import android.view.View;

public interface BannerEventListener {

	void onBannerLoaded(View bannerView);

	void onBannerClick(View bannerView);

	void onBannerError(View bannerView, String error);

	void onBannerLeftApplication(View bannerView);
}
