package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.applovin.AppLovinMediationAdapter;

public final class AppLovinCompatibilityAdapter extends AppLovinMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
