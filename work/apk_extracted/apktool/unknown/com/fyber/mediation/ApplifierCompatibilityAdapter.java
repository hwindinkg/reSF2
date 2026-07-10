package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.unityads.UnityAdsMediationAdapter;

public final class ApplifierCompatibilityAdapter extends UnityAdsMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
