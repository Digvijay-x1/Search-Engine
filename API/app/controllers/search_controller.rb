class SearchController < ApplicationController
  def index
    @query = params[:q]
    @results = []

    if @query.present?
      # "ranker_service" is the DNS name defined in docker-compose
      response = HTTParty.get("http://ranker_service:5000/search?q=#{@query}")
      @results = JSON.parse(response.body)["results"]
    end
  end
end
