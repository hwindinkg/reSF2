package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.adcolony.AdColonyMediationAdapter;

public final class AdColonyCompatibilityAdapter extends AdColonyMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
