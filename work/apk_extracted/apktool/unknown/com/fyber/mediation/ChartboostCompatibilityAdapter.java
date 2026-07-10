package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.chartboost.ChartboostMediationAdapter;

public final class ChartboostCompatibilityAdapter extends ChartboostMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
