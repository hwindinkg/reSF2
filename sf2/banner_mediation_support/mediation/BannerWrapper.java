/**
 * Fyber Android SDK
 * <p>
 * Copyright (c) 2016 Fyber. All rights reserved.
 */
package com.fyber.ads.banners.mediation;

import android.view.View;

public abstract class BannerWrapper {

	public abstract View getView();


	public void onBannerLoaded() {
	}

	public void onBannerClick() {
	}

	public void onBannerError(String error) {
	}

	public void onBannerLeftApplication() {
	}

	public void setBannerEventListener(BannerEventListener bannerEventListener) {
	}
}
