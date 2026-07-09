/*
 * Fyber Android SDK Copyright (c) 2015 Fyber. All rights reserved.
 */
package com.fyber.ads.banners.mediation;

import android.content.Context;

import com.fyber.ads.banners.BannerSize;
import com.fyber.mediation.MediationAdapter;

import java.util.List;

public abstract class BannerMediationAdapter<V extends MediationAdapter> {


	public BannerMediationAdapter(V adapter) {
	}

	protected abstract boolean checkForAds(Context context, List<BannerSize> bannerSizes);

	protected void setAdAvailable(BannerWrapper bannerWrapper) {
	}

	protected void setAdNotAvailable() {
	}

	protected void setAdError(String error) {
	}

}
